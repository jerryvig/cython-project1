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
    memset(timestamps[0], 0, sizeof timestamps[0]);
    memset(timestamps[1], 0, sizeof timestamps[1]);
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

static int8_t get_crumb(const char *response_text, char *crumb) {
    const char *crumbstore = strstr(response_text, "CrumbStore");
    if (crumbstore == NULL) {
        puts("Failed to find crumbstore....");
        return 1;
    }
    const char *colon_quote = strstr(crumbstore, ":\"");
    const char *end_quote = strstr(&colon_quote[2], "\"");
    char crumbclean[128];
    memset(crumbclean, 0, sizeof crumbclean);
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) -
        strlen(end_quote));
    const char *twofpos = strstr(crumb, "\\u002F");

    if (twofpos) {
        strncpy(crumbclean, crumb, twofpos - crumb);
        strcat(crumbclean, "%2F");
        strcat(crumbclean, &twofpos[6]);
        memset(crumb, 0, sizeof crumb);
        strcpy(crumb, crumbclean);
    }
    return 0;
}

int get_title(const char *response_text, char *title) {
    printf("in get_title()\n");
    memset(title, 0, sizeof title);
    const char* title_start = strstr(response_text, "<title>");
    if (!title_start) {
        fprintf(stderr, "Failed to find the <title> tag in the response.\n");
        return 1;
    }
    const char* parens_start = strstr(title_start, "(");

    const size_t diff = strlen(&title_start[7]) - strlen(parens_start);
    if (diff < 128) {
        strncpy(title, &title_start[7], diff);
        return 0;
    }
    fprintf(stderr, "Failed to parse the title from the response.\n");
    return 1;
}

int16_t get_adj_close_and_changes(char *response_text, double *changes, int64_t *daily_volume) {
    register int16_t i = 0;
    register double adj_close;
    register double last_adj_close;
    char line[512];
    char *cols;
    char adj_close_str[128];
    char volume_str[128];
    register char *last_column;

    register char *token = strtok(response_text, "\n");
    while (token) {
        if (i) {
            memset(line, 0, sizeof line);
            memset(adj_close_str, 0, sizeof adj_close_str);
            memset(volume_str, 0, sizeof volume_str);
            strcpy(line, token);

            cols = strstr(&line[1], ",");

            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");
            cols = strstr(&cols[1], ",");

            last_column = strstr(&cols[1], ",");
            strncpy(adj_close_str, &cols[1], strlen(&cols[1])
                - strlen(last_column));
            strcpy(volume_str, &last_column[1]);

            if (strcmp(adj_close_str, "null") == 0) {
                fprintf(stderr, "\"null\" found in data...returning 0...\n");
                return 0;
            }

            adj_close = atof(adj_close_str);

            if (i > 1) {
                changes[i-2] = (adj_close - last_adj_close)/last_adj_close;
                daily_volume[i-2] = atoll(volume_str);
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
    double *self_correlation;
} gsl_correlation_args;

void *gsl_correlation_thread_proc(void *gsl_args) {
    gsl_correlation_args *gsl_corr_args = (gsl_correlation_args*)gsl_args;
    const double sc = gsl_stats_correlation(gsl_corr_args->data1, gsl_corr_args->stride1, gsl_corr_args->data2, gsl_corr_args->stride2, gsl_corr_args->n);
    *(gsl_corr_args->self_correlation) = sc;
    return (void*)&sc;
}

typedef struct {
    changes_tuple *changes_tuples;
    size_t count;
} qsort_proc_args;

void *qsort_thread_proc(void *args) {
    qsort_proc_args *qsort_args = (qsort_proc_args*)args;
    changes_tuple *changes_tuples = qsort_args->changes_tuples;
    qsort(changes_tuples, qsort_args->count, sizeof(changes_tuple), compare_changes_tuples);
}

static void compute_sign_diff_pct(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values) {
    register int16_t i;
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
    double self_correlation;
    gsl_correlation_args gsl_corr_args;
    gsl_corr_args.data1 = changes_minus_one;
    gsl_corr_args.stride1 = 1;
    gsl_corr_args.data2 = changes_0;
    gsl_corr_args.stride2 = 1;
    gsl_corr_args.n = changes_length - 2;
    gsl_corr_args.self_correlation = &self_correlation;

    pthread_t qsort_thread;
    qsort_proc_args qsort_args;
    qsort_args.changes_tuples = changes_tuples;
    qsort_args.count = changes_length - 2;

    pthread_create( &qsort_thread, NULL, qsort_thread_proc, (void*)&qsort_args );
    pthread_create( &gsl_thread, NULL, gsl_correlation_thread_proc, (void*)&gsl_corr_args );
   
    pthread_join( gsl_thread, NULL );
    pthread_join( qsort_thread, NULL );

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

void get_sigma_data(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values) {
    const double stdev  = gsl_stats_sd(changes_daily, 1, (changes_length - 1));
    const double sigma_change = changes_daily[changes_length - 1]/stdev;

    compute_sign_diff_pct(changes_daily, changes_length, sign_diff_values);

    sprintf(sign_diff_values->change, "%.3f%%", changes_daily[changes_length - 1] * 100);
    sprintf(sign_diff_values->sigma, "%.3f%%", stdev * 100);
    sprintf(sign_diff_values->sigma_change, "%.3f", sigma_change);
    sprintf(sign_diff_values->record_count, "%d", changes_length);
}

void process_tickers(char *ticker_string, curl_multi_ez_t *curl_multi_ez, char timestamps[][12]) {
    char sign_diff_print[512];
    char *ticker = strsep(&ticker_string, " ");

    while (ticker != NULL) {
        sign_diff_pct sign_diff_values;
        run_stats(ticker, &sign_diff_values, curl_multi_ez->ez_pool[0], timestamps);
        memset(sign_diff_print, 0, 512);
        build_sign_diff_print_string(sign_diff_print, &sign_diff_values);
        printf("%s", sign_diff_print);

        ticker = strsep(&ticker_string, " ");
    }
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    const size_t rs = size * nmemb;
    memory_t *mem = (memory_t*)userp;
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
        memset(ticker, 0, 16);
        strncpy(ticker, &filename[9], strlen(&filename[9]) - strlen(period));
    }
    return nitems * size;
}

static char crumb[128];

char *prime_crumb(curl_multi_ez_t *curl_multi_ez) {
    CURL *ez = curl_multi_ez->ez_pool[0];
    curl_easy_setopt(ez, CURLOPT_URL, "https://finance.yahoo.com/quote/AAPL/history");

    void *private;
    curl_easy_getinfo(ez, CURLINFO_PRIVATE, &private);

    const CURLcode response = curl_easy_perform(ez);
    if (response != CURLE_OK) {
        printf("curl_easy_perform() failed.....\n");
    }

    private_data_t *private_data = (private_data_t*)private;

    memset(crumb, 0, sizeof crumb);
    int8_t crumb_failure = get_crumb(private_data->buffer->memory, crumb);

    reset_private_data(private_data);

    if (crumb_failure) {
        fprintf(stderr, "Failed to prime crumb...\n");
    } else {
        fprintf(stderr, "primed crumb = \"%s\"\n", crumb);
    }

    // set/get cookie here.
    struct curl_slist *cookies_root;
    CURLcode res = curl_easy_getinfo(ez, CURLINFO_COOKIELIST, &cookies_root);
    if (res != CURLE_OK) {
        fprintf(stderr, "Failed to get root of the cookies list...\n");
    } else {
        for (register int8_t i = 1; i < EZ_POOL_SIZE; ++i) {
            curl_easy_setopt(curl_multi_ez->ez_pool[i], CURLOPT_COOKIELIST, cookies_root->data);
        }
    }
    curl_slist_free_all(cookies_root);

    return crumb;
}

void run_stats(const char *ticker_string, sign_diff_pct *sign_diff_values, const CURL *curl, char timestamps[][12]) {
    char ticker_str[128];
    memset(ticker_str, 0, 128);
    const register int ticker_strlen = strlen(ticker_string);
    strncpy(ticker_str, ticker_string, ticker_strlen);

    for (register int8_t i = 0; i < ticker_strlen; ++i) {
        if (ticker_string[i] != '\n') {
            ticker_str[i] = toupper(ticker_str[i]);
        } else {
            ticker_str[i] = 0;
        }
    }

    //We don't need to do this first request if the crumb is already set,
    //but we need the http response html to extract the title.
    if (crumb == NULL) {
        memory_t memoria;
        memoria.memory = (char*)malloc(1);
        memoria.size = 0;
        curl_easy_setopt((CURL*)curl, CURLOPT_WRITEDATA, (void*)&memoria);

        char url[128];
        memset(url, 0, 128);
        sprintf(url, "https://finance.yahoo.com/quote/%s/history", ticker_str);
        curl_easy_setopt((CURL*)curl, CURLOPT_URL, url);

        //This should be refactored to a non-blocking call using curl_multi
        CURLcode response = curl_easy_perform(curl);
        if (response != CURLE_OK) {
            printf("curl_easy_perform() failed.....\n");
        }

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
    // printf("url = %s\n", download_url);
    curl_easy_setopt(curl, CURLOPT_URL, download_url);

    // struct timespec start;
    // struct timespec end;
    // clock_gettime(CLOCK_MONOTONIC, &start);

    memory_t dl_memoria;
    dl_memoria.memory = (char*)malloc(1);
    dl_memoria.size = 0;

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&dl_memoria);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&(sign_diff_values->response_ticker[0]));

    CURLcode curl_response = curl_easy_perform(curl);

    if (curl_response != CURLE_OK) {
        printf("curl_easy_perform() failed.....\n");
    }

    double changes_daily[512];
    int64_t daily_volume[512];
    const int16_t changes_length = get_adj_close_and_changes(dl_memoria.memory, changes_daily, daily_volume);

    free(dl_memoria.memory);

    if (!changes_length) {
        printf("Failed to parse adj_close and changes data from response.\n");
        return;
    }

    get_sigma_data(changes_daily, changes_length, sign_diff_values);
}

CURL *create_and_init_curl(void) {
    memory_t *buffer = (memory_t*)malloc(sizeof(memory_t));
    buffer->memory = (char*)malloc(1);
    buffer->size = 0;
    private_data_t *private_data = (private_data_t*)malloc(sizeof(private_data_t));
    private_data->buffer = buffer;
    memset(private_data->ticker_string, 0, sizeof private_data->ticker_string);

    CURL *ez = curl_easy_init();
    private_data->ez = ez;

    curl_easy_setopt(ez, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(ez, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(ez, CURLOPT_ACCEPT_ENCODING, "br, gzip");
    curl_easy_setopt(ez, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(ez, CURLOPT_TCP_KEEPIDLE, 180L);
    curl_easy_setopt(ez, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(ez, CURLOPT_TCP_FASTOPEN, 1L);
    curl_easy_setopt(ez, CURLOPT_TCP_NODELAY, 0);
    curl_easy_setopt(ez, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(ez, CURLOPT_WRITEFUNCTION, &write_callback);
    curl_easy_setopt(ez, CURLOPT_WRITEDATA, (void*)buffer);
    curl_easy_setopt(ez, CURLOPT_HEADERFUNCTION, &header_callback);
    curl_easy_setopt(ez, CURLOPT_HEADERDATA, (void*)&(private_data->ticker_string));
    curl_easy_setopt(ez, CURLOPT_PRIVATE, (void*)private_data);
    return ez;
}

#define TEMP_STRLEN 128
void build_sign_diff_print_string(char sign_diff_print[], sign_diff_pct *sign_diff_values) {
    char temp_str[TEMP_STRLEN] = {'\0'};
    memset(sign_diff_print, 0, sizeof sign_diff_print);

    strcat(sign_diff_print, "===============================\n");
    sprintf(temp_str, "  \"avg_move_10_down\": %s\n  \"avg_move_10_up\": %s\n", sign_diff_values->avg_move_10_down, sign_diff_values->avg_move_10_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);

    sprintf(temp_str, "  \"title\": \"%s\"\n  \"resp_ticker\": %s\n", sign_diff_values->title, sign_diff_values->response_ticker);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"change\": \"%s\"\n", sign_diff_values->change);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"record_count\": %s\n  \"self_correlation\": %s\n", sign_diff_values->record_count, sign_diff_values->self_correlation);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"sigma\": %s\n  \"sigma_change\": %s\n", sign_diff_values->sigma, sign_diff_values->sigma_change);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"sign_diff_pct_10_down\": %s\n  \"sign_diff_pct_10_up\": %s\n", sign_diff_values->sign_diff_pct_10_down, sign_diff_values->sign_diff_pct_10_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"sign_diff_pct_20_down\": %s\n  \"sign_diff_pct_20_up\": %s\n", sign_diff_values->sign_diff_pct_20_down, sign_diff_values->sign_diff_pct_20_up);
    strcat(sign_diff_print, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "  \"stdev_10_down\": %s\n  \"stdev_10_up\": %s\n", sign_diff_values->stdev_10_down, sign_diff_values->stdev_10_up);
    strcat(sign_diff_print, temp_str);
}

void build_sign_diff_print_json(char sign_diff_json[], sign_diff_pct *sign_diff_values) {
    char temp_str[TEMP_STRLEN] = {'\0'};
    memset(sign_diff_json, 0, sizeof sign_diff_json);

    sprintf(temp_str, "{\"avg_move_10_down\":\"%s\",",
        sign_diff_values->avg_move_10_down);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"avg_move_10_up\":\"%s\",",
        sign_diff_values->avg_move_10_up);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"title\":\"%s\",", sign_diff_values->title);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"resp_ticker\":\"%s\",",
        sign_diff_values->response_ticker);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"change\":\"%s\",", sign_diff_values->change);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"record_count\":%s,", sign_diff_values->record_count);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"self_correlation\":\"%s\",",
        sign_diff_values->self_correlation);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sigma\":\"%s\",", sign_diff_values->sigma);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sigma_change\":%s,", sign_diff_values->sigma_change);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sign_diff_pct_10_down\":\"%s\",",
        sign_diff_values->sign_diff_pct_10_down);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sign_diff_pct_10_up\":\"%s\",",
        sign_diff_values->sign_diff_pct_10_up);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sign_diff_pct_20_down\":\"%s\",",
        sign_diff_values->sign_diff_pct_20_down);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"sign_diff_pct_20_up\":\"%s\",",
        sign_diff_values->sign_diff_pct_20_up);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"stdev_10_down\":\"%s\",",
        sign_diff_values->stdev_10_down);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, TEMP_STRLEN);
    sprintf(temp_str, "\"stdev_10_up\":\"%s\"}", sign_diff_values->stdev_10_up);
    strcat(sign_diff_json, temp_str);
}
