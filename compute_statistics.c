//Build with: gcc compute_statistics.c -o compute_statistics -O3 -pedantic -lcurl -lgsl -lgslcblas -Wall -Wextra -std=c11
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <gsl/gsl_statistics_double.h>
#include "compute_statistics.h"

typedef struct {
    char *memory;
    size_t size;
} Memory;

typedef struct {
    double change_0;
    double change_plus_one;
} changes_tuple;

int compare_changes_tuples(const void *a, const void *b) {
    const changes_tuple a_val = ((const changes_tuple*)a)[0];
    const changes_tuple b_val = ((const changes_tuple*)b)[0];

    if (a_val.change_0 < b_val.change_0) {
        return 1;
    }
    if (a_val.change_0 > b_val.change_0) {
        return -1;
    }
    return 0;
}

void get_timestamps(char timestamps[][12]) {
    memset(timestamps[0], 0, 12);
    memset(timestamps[1], 0, 12);
    const time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    now_tm->tm_sec = 0;
    now_tm->tm_min = 0;
    now_tm->tm_hour = 0;
    const time_t today_time = mktime(now_tm);
    const time_t manana = today_time + 86400;
    const time_t ago_366_days = today_time - 31622400;
    sprintf(timestamps[0], "%ld", manana);
    sprintf(timestamps[1], "%ld", ago_366_days);
}

static int get_crumb(const char *response_text, char *crumb) {
    const char *crumbstore = strstr(response_text, "CrumbStore");
    if (crumbstore == NULL) {
        puts("Failed to find crumbstore....");
        return 1;
    }
    const char *colon_quote = strstr(crumbstore, ":\"");
    const char *end_quote = strstr(&colon_quote[2], "\"");
    char crumbclean[128];
    memset(crumbclean, 0, 128);
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) - strlen(end_quote));
    const char *twofpos = strstr(crumb, "\\u002F");

    if (twofpos) {
        strncpy(crumbclean, crumb, twofpos - crumb);
        strcat(crumbclean, "%2F");
        strcat(crumbclean, &twofpos[6]);
        memset(crumb, 0, 128);
        strcpy(crumb, crumbclean);
    }
    return 0;
}

static int get_title(const char *response_text, char *title) {
    memset(title, 0, 128);
    const char* title_start = strstr(response_text, "<title>");
    const char* pipe_start = strstr(title_start, "|");
    const char* hyphen_end = strstr(&pipe_start[2], "-");
    const size_t diff = strlen(&pipe_start[2]) - strlen(hyphen_end);
    if (diff < 128) {
        strncpy(title, &pipe_start[2], diff);
        return 0;
    }
    printf("Failed to parse the title from the response.\n");
    return 1;
}

static int get_adj_close_and_changes(char *response_text, double *changes) {
    register int i = 0;
    register double adj_close;
    register double last_adj_close;
    char line[512];
    char *cols;
    char adj_close_str[128];
    register char *last_column;

    register char *token = strtok(response_text, "\n");
    while (token) {
        if (i) {
            memset(line, 0, 512);
            memset(adj_close_str, 0, 128);
            strcpy(line, token);

            cols = strstr(&line[1], ",");

            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");
            
            last_column = strstr(&cols[1], ",");
            strncpy(adj_close_str, &cols[1], strlen(&cols[1]) - strlen(last_column));
            if (strcmp(adj_close_str, "null") == 0) {
                return 0;
            }

            adj_close = atof(adj_close_str);
            if (i > 1) {
                changes[i-2] = (adj_close - last_adj_close)/last_adj_close;
            }
            last_adj_close = adj_close;
        }
        token = strtok(NULL, "\n");
        i++;
    }
    return i - 2;
}

typedef struct {
    double *data1;
    size_t stride1;
    double *data2;
    size_t stride2;
    size_t n;
} gsl_correlation_args;

void *gsl_correlation_thread_proc( void *gsl_args ) {
    gsl_correlation_args *gsl_corr_args = (gsl_correlation_args*)gsl_args;
    const double sc = gsl_stats_correlation(gsl_corr_args->data1, gsl_corr_args->stride1, gsl_corr_args->data2, gsl_corr_args->stride2, gsl_corr_args->n);
    double *self_correlation = (double*)malloc(sizeof(double));
    *self_correlation = sc;
    return (void*)self_correlation;
}

typedef struct {
    changes_tuple *changes_tuples;
    size_t count;
} qsort_proc_args;

void *qsort_thread_proc( void *args ) {
    qsort_proc_args *qsort_args = (qsort_proc_args*)args;
    changes_tuple *changes_tuples = qsort_args->changes_tuples;
    qsort(changes_tuples, qsort_args->count, sizeof(changes_tuple), compare_changes_tuples);
}

static void compute_sign_diff_pct(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values) {
    register int i;
    double changes_minus_one[changes_length - 2];
    double changes_0[changes_length - 2];
    changes_tuple changes_tuples[changes_length - 2];

    for (i = 0; i < changes_length - 2; ++i) {
        changes_minus_one[i] = changes_daily[i];
        changes_0[i] = changes_daily[i+1];
        changes_tuples[i].change_0 = changes_daily[i];
        changes_tuples[i].change_plus_one = changes_daily[i+1];
    }

    //struct timespec start;
    //struct timespec end;
    //clock_gettime(CLOCK_MONOTONIC, &start);
    
    pthread_t gsl_thread;
    gsl_correlation_args gsl_corr_args;
    gsl_corr_args.data1 = changes_minus_one;
    gsl_corr_args.stride1 = 1;
    gsl_corr_args.data2 = changes_0;
    gsl_corr_args.stride2 = 1;
    gsl_corr_args.n = changes_length - 2;
    void *sc;

    pthread_t qsort_thread;
    qsort_proc_args qsort_args;
    qsort_args.changes_tuples = changes_tuples;
    qsort_args.count = changes_length - 2;

    pthread_create( &qsort_thread, NULL, qsort_thread_proc, (void*)&qsort_args );
    pthread_create( &gsl_thread, NULL, gsl_correlation_thread_proc, (void*)&gsl_corr_args );
   
    pthread_join( gsl_thread, &sc );
    pthread_join( qsort_thread, NULL );

    const double self_correlation = *((double*)sc);
    free(sc);

    //clock_gettime(CLOCK_MONOTONIC, &end);
    //printf("self_correlation and qsort proc'ed in %.6f s\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec));

    double np_avg_10_up[10];
    double np_avg_10_down[10];
    int pct_sum_10_up = 0;
    int pct_sum_10_down = 0;
    int pct_sum_20_up = 0;
    int pct_sum_20_down = 0;
    double product_up;
    double product_down;

    for (i = 0; i < 20; ++i) {
        product_up = changes_tuples[i].change_0 * changes_tuples[i].change_plus_one;
        product_down = changes_tuples[changes_length - 22 + i].change_0 * changes_tuples[changes_length - 22 + i].change_plus_one;

        if (i < 10) {
            np_avg_10_up[i] = changes_tuples[i].change_plus_one;
            np_avg_10_down[i] = changes_tuples[changes_length - 12 + i].change_plus_one;
        }

        if (product_up < 0) {
            pct_sum_20_up++;
            if (i < 10) {
                pct_sum_10_up++;
            }
        }

        if (product_down < 0) {
            pct_sum_20_down++;
            if (i > 9) {
                pct_sum_10_down++;
            }
        }
    }

    const double avg_10_up = gsl_stats_mean(np_avg_10_up, 1, 10);
    const double stdev_10_up = gsl_stats_sd(np_avg_10_up, 1, 10);

    const double avg_10_down = gsl_stats_mean(np_avg_10_down, 1, 10);
    const double stdev_10_down = gsl_stats_sd(np_avg_10_down, 1, 10);

    sprintf(sign_diff_values->avg_move_10_up, "%.4f%%", avg_10_up * 100);
    sprintf(sign_diff_values->avg_move_10_down, "%.4f%%", avg_10_down * 100);
    sprintf(sign_diff_values->stdev_10_up, "%.4f%%", stdev_10_up * 100);
    sprintf(sign_diff_values->stdev_10_down, "%.4f%%", stdev_10_down * 100);
    sprintf(sign_diff_values->self_correlation, "%.3f%%", self_correlation * 100);
    sprintf(sign_diff_values->sign_diff_pct_10_up, "%.0f%%", pct_sum_10_up * 10.0);
    sprintf(sign_diff_values->sign_diff_pct_10_down, "%.0f%%", pct_sum_10_down * 10.0);
    sprintf(sign_diff_values->sign_diff_pct_20_up, "%.0f%%", pct_sum_20_up * 5.0);
    sprintf(sign_diff_values->sign_diff_pct_20_down, "%.0f%%", pct_sum_20_down * 5.0);
}

static void get_sigma_data(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values) {
    const double stdev  = gsl_stats_sd(changes_daily, 1, (changes_length - 1));
    const double sigma_change = changes_daily[changes_length - 1]/stdev;

    compute_sign_diff_pct(changes_daily, changes_length, sign_diff_values);

    sprintf(sign_diff_values->change, "%.3f%%", changes_daily[changes_length - 1] * 100);
    sprintf(sign_diff_values->sigma, "%.3f%%", stdev * 100);
    sprintf(sign_diff_values->sigma_change, "%.3f", sigma_change);
    sprintf(sign_diff_values->record_count, "%d", changes_length);
}

void process_tickers(char *ticker_string, CURL *curl, char timestamps[][12]) {
    char sign_diff_print[512];
    char *ticker = strsep(&ticker_string, " ");

    while (ticker != NULL) {
        sign_diff_pct sign_diff_values;
        run_stats(ticker, &sign_diff_values, curl, timestamps);
        memset(sign_diff_print, 0, 512);
        build_sign_diff_print_string(sign_diff_print, &sign_diff_values);
        printf("%s", sign_diff_print);

        ticker = strsep(&ticker_string, " ");
        if (ticker != NULL) {
            usleep(1500000);
        }
    }
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    const size_t rs = size * nmemb;
    Memory *mem = (Memory*)userp;
    char *ptr = (char*)realloc(mem->memory, mem->size + rs + 1);
    if (ptr == NULL) {
        printf("Insufficient memory: realloc() returned NULL.\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, rs);
    mem->size += rs;
    mem->memory[mem->size] = 0;
    return rs;
}

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    char *prefix = "content-disposition";
    if (strncmp(prefix, buffer, strlen(prefix)) == 0) {
        char *filename = strstr(buffer, "filename=");
        char *period = strstr(&filename[9], ".");
        char *ticker = (char*)userdata;
        memset(ticker, 0, 8);
        strncpy(ticker, &filename[9], strlen(&filename[9]) - strlen(period));
    }
    return nitems * size;
}

void *curl_thread_proc( void *curl_ptr ) {
    CURL *curl = (CURL*)curl_ptr;
    CURLcode response = curl_easy_perform(curl);
    CURLcode *response_ptr = (CURLcode*)malloc(sizeof(CURLcode));
    *response_ptr = response;
    return (void*)response_ptr;
}

static char *crumb;

void run_stats(const char *ticker_string, sign_diff_pct *sign_diff_values, CURL *curl, char timestamps[][12]) {
    char ticker_str[128];
    memset(ticker_str, 0, 128);
    register int ticker_strlen = strlen(ticker_string);
    strncpy(ticker_str, ticker_string, ticker_strlen);

    for (register int i = 0; i < ticker_strlen; ++i) {
        if (ticker_string[i] != '\n') {
            ticker_str[i] = toupper(ticker_str[i]);
        } else {
            ticker_str[i] = NULL;
        }
    }

    //We don't need to do this first request if the crumb is already set,
    //but we need the http response html to extract the title.
    if (crumb == NULL) {
        Memory memoria;
        memoria.memory = (char*)malloc(1);
        memoria.size = 0;
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&memoria);

        char url[128];
        memset(url, 0, 128);
        sprintf(url, "https://finance.yahoo.com/quote/%s/history", ticker_str);
        curl_easy_setopt(curl, CURLOPT_URL, url);

        //This should be refactored to a non-blocking call using curl_multi
        CURLcode response = curl_easy_perform(curl);
        if (response != CURLE_OK) {
            printf("curl_easy_perform() failed.....\n");
        }

        crumb = (char*)malloc(128 * sizeof(char));
        memset(crumb, 0, 128);
        int crumb_failure = get_crumb(memoria.memory, crumb);
        if (crumb_failure) {
            return;
        }

        int title_failure = get_title(memoria.memory, sign_diff_values->title);
        if (title_failure) {
            return;
        }
        free(memoria.memory);
    } else {
        strcpy(sign_diff_values->title, "N/A");
    } //end if (crumb == NULL)

    char download_url[256];
    memset(download_url, 0, 256);
    sprintf(download_url, "https://query1.finance.yahoo.com/v7/finance/download/%s?period1=%s&period2=%s&interval=1d&events=history&crumb=%s", ticker_str, timestamps[1], timestamps[0], crumb);
    printf("download_url = %s\n", download_url);
    curl_easy_setopt(curl, CURLOPT_URL, download_url);

    Memory dl_memoria;
    dl_memoria.memory = (char*)malloc(1);
    dl_memoria.size = 0;

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&dl_memoria);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&(sign_diff_values->response_ticker[0]));

    pthread_t curl_thread;
    void *curl_return_value;

    pthread_create( &curl_thread, NULL, curl_thread_proc, (void*)curl );

    //Any asynchronous tasks would be implemented here.

    pthread_join( curl_thread, &curl_return_value );

    CURLcode *curl_response = (CURLcode*)curl_return_value;
    if (*curl_response != CURLE_OK) {
        printf("curl_easy_perform() failed.....\n");
    }
    free(curl_response);

    double changes_daily[512];
    const int changes_length = get_adj_close_and_changes(dl_memoria.memory, changes_daily);

    free(dl_memoria.memory);

    if (!changes_length) {
        printf("Failed to parse adj_close and changes data from response.\n");
        return;
    }

    get_sigma_data(changes_daily, changes_length, sign_diff_values);
}

CURL *create_and_init_curl(void) {
    const CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "br, gzip");
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 180L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_callback);
    return curl;
}

void build_sign_diff_print_string(char sign_diff_print[], sign_diff_pct *sign_diff_values) {
    const int temp_strlen = 128;
    char temp_str[temp_strlen];
    memset(sign_diff_print, 0, 512);
    memset(temp_str, 0, temp_strlen);

    strcat(sign_diff_print, "===============================\n");
    sprintf(temp_str, "  \"avg_move_10_down\": %s\n  \"avg_move_10_up\": %s\n", sign_diff_values->avg_move_10_down, sign_diff_values->avg_move_10_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"title\": \"%s\"\n  \"resp_ticker\": %s\n", sign_diff_values->title, sign_diff_values->response_ticker);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"change\": \"%s\"\n", sign_diff_values->change);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"record_count\": %s\n  \"self_correlation\": %s\n", sign_diff_values->record_count, sign_diff_values->self_correlation);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"sigma\": %s\n  \"sigma_change\": %s\n", sign_diff_values->sigma, sign_diff_values->sigma_change);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"sign_diff_pct_10_down\": %s\n  \"sign_diff_pct_10_up\": %s\n", sign_diff_values->sign_diff_pct_10_down, sign_diff_values->sign_diff_pct_10_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"sign_diff_pct_20_down\": %s\n  \"sign_diff_pct_20_up\": %s\n", sign_diff_values->sign_diff_pct_20_down, sign_diff_values->sign_diff_pct_20_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "  \"stdev_10_down\": %s\n  \"stdev_10_up\": %s\n", sign_diff_values->stdev_10_down, sign_diff_values->stdev_10_up);
    strcat(sign_diff_print, temp_str);
}
