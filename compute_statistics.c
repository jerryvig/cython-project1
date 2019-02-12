//Build with: gcc compute_statistics.c -o compute_statistics -O3 -pedantic -lcurl -lgsl -lgslcblas -Wall -Wextra -std=c11
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gsl/gsl_statistics_double.h>

typedef struct {
    char *memory;
    size_t size;
} Memory;

typedef struct {
    double change_0;
    double change_plus_one;
} changes_tuple;

typedef struct {
    char avg_move_10_up[16];
    char avg_move_10_down[16];
    char change[16];
    char record_count[8];
    char self_correlation[16];
    char sigma[16];
    char sigma_change[16];
    char stdev_10_up[16];
    char stdev_10_down[16];
    char sign_diff_pct_10_up[16];
    char sign_diff_pct_20_up[16];
    char sign_diff_pct_10_down[16];
    char sign_diff_pct_20_down[16];
    char title[128];
} sign_diff_pct;

int compare_changes_tuples(const void *a, const void *b) {
    changes_tuple a_val = ((const changes_tuple*)a)[0];
    changes_tuple b_val = ((const changes_tuple*)b)[0];

    if (a_val.change_0 < b_val.change_0) {
        return 1;
    }
    if (a_val.change_0 > b_val.change_0) {
        return -1;
    }
    return 0;
}

static void get_timestamps(char timestamps[][12]) {
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
    int i = 0;
    double adj_close;
    double last_adj_close;
    char line[512];
    char *cols;
    char adj_close_str[128];
    char *last_column;

    char *token = strtok(response_text, "\n");
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

    const double self_correlation = gsl_stats_correlation(changes_minus_one, 1, changes_0, 1, changes_length - 2);
    qsort(changes_tuples, changes_length - 2, sizeof(changes_tuple), compare_changes_tuples);

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
    sprintf(sign_diff_values->sign_diff_pct_10_up, "%.1f%%", pct_sum_10_up * 10.0);
    sprintf(sign_diff_values->sign_diff_pct_10_down, "%.1f%%", pct_sum_10_down * 10.0);
    sprintf(sign_diff_values->sign_diff_pct_20_up, "%.1f%%", pct_sum_20_up * 5.0);
    sprintf(sign_diff_values->sign_diff_pct_20_down, "%.1f%%", pct_sum_20_down * 5.0);
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

static void process_ticker(char *ticker, char timestamps[][12], CURL *curl) {
    struct timespec start;
    struct timespec end;

    char url[128];
    memset(url, 0, 128);
    sprintf(url, "https://finance.yahoo.com/quote/%s/history?p=%s", ticker, ticker);
    // printf("url = %s\n", url);

    CURLcode response;
    Memory memoria;
    memoria.memory = (char*)malloc(1);
    memoria.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&memoria);

    response = curl_easy_perform(curl);

    if (response != CURLE_OK) {
        printf("curl_easy_perform() failed.....\n");
    }

    char crumb[128];
    memset(crumb, 0, 128);

    sign_diff_pct sign_diff_values;
    memset(sign_diff_values.title, 0, 128);

    int crumb_failure = get_crumb(memoria.memory, crumb);
    if (crumb_failure) {
        printf("Failed to get crumb...\n");
        return;
    }

    int title_failure = get_title(memoria.memory, sign_diff_values.title);
    if (title_failure) {
        return;
    }

    char download_url[256];
    memset(download_url, 0, 256);
    sprintf(download_url, "https://query1.finance.yahoo.com/v7/finance/download/%s?period1=%s&period2=%s&interval=1d&events=history&crumb=%s", ticker, timestamps[1], timestamps[0], crumb);
    printf("download_url = %s\n", download_url);

    free(memoria.memory);
    memoria.memory = (char*)malloc(1);
    memoria.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, download_url);
    response = curl_easy_perform(curl);

    if (response != CURLE_OK) {
        printf("curl_easy_perform() failed.....\n");
        return;
    }

    double changes_daily[512];
    const int changes_length = get_adj_close_and_changes(memoria.memory, changes_daily);

    free(memoria.memory);

    if (!changes_length) {
        return;
    }

    get_sigma_data(changes_daily, changes_length, &sign_diff_values);

    printf("===============================\n");
    printf("  \"avg_move_10_down\": %s\n  \"avg_move_10_up\": %s\n", sign_diff_values.avg_move_10_down, sign_diff_values.avg_move_10_up);
    printf("  \"title\": \"%s\"\n  \"change\": %s\n", sign_diff_values.title, sign_diff_values.change);
    printf("  \"record_count\": %s\n  \"self_correlation\": %s\n", sign_diff_values.record_count, sign_diff_values.self_correlation);
    printf("  \"sigma\": %s\n  \"sigma_change\": %s\n", sign_diff_values.sigma, sign_diff_values.sigma_change);
    printf("  \"sign_diff_pct_10_down\": %s\n  \"sign_diff_pct_10_up\": %s\n", sign_diff_values.sign_diff_pct_10_down, sign_diff_values.sign_diff_pct_10_up);
    printf("  \"sign_diff_pct_20_down\": %s\n  \"sign_diff_pct_20_up\": %s\n", sign_diff_values.sign_diff_pct_20_down, sign_diff_values.sign_diff_pct_20_up);
    printf("  \"stdev_10_down\": %s\n  \"stdev_10_up\": %s\n", sign_diff_values.stdev_10_down, sign_diff_values.stdev_10_up);
}

static void process_tickers(char *ticker_string, char timestamps[][12], CURL *curl) {
    char *ticker = strsep(&ticker_string, " ");
    while (ticker != NULL) {
        process_ticker(ticker, timestamps, curl);
        ticker = strsep(&ticker_string, " ");
        if (ticker != NULL) {
            usleep(1500000);
        }
    }
}

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
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

void run_stats(const char *ticker_string) {
    const CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);

    char ticker_str[128];
    memset(ticker_str, 0, 128);
    int ticker_strlen = strlen(ticker_string);
    strncpy(ticker_str, ticker_string, ticker_strlen);

    char timestamps[2][12];
    get_timestamps(timestamps);

    for (register int i = 0; i < ticker_strlen; ++i) {
        if (ticker_string[i] != '\n') {
            ticker_str[i] = toupper(ticker_str[i]);
        } else {
            ticker_str[i] = NULL;
        }
    }

    process_tickers(ticker_str, timestamps, curl);

    curl_easy_cleanup(curl);
}


char *get_ts(char *ticker) {
    
    
    char url[128];
    memset(url, 0, 128);
    sprintf(url, "https://finance.yahoo.com/quote/%s/history?p=%s", ticker, ticker);
}

int main(void) {
    const CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);

    char timestamps[2][12];
    get_timestamps(timestamps);
    struct timespec start;
    struct timespec end;

    register int i;
    int ticker_strlen;
    char ticker_string[128];

    while (1) {
        memset(ticker_string, 0, 128);
        printf("%s", "Enter ticker list: ");
        fflush(stdout);
        fgets(ticker_string, 128, stdin);
        
        ticker_strlen = strlen(ticker_string) - 1;
        if (!ticker_strlen) {
            printf("Got empty ticker string....\n");
            continue;
        }

        ticker_string[ticker_strlen] = NULL;
        for (i = 0; i < ticker_strlen; ++i) {
            ticker_string[i] = toupper(ticker_string[i]);
        }
        
        clock_gettime(CLOCK_MONOTONIC, &start);
        process_tickers(ticker_string, timestamps, curl);
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("processed in %.5f s\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec));
    }
    curl_easy_cleanup(curl);
    
    return EXIT_SUCCESS;
}
