#ifndef COMPUTE_STATISTICS_H_
#define COMPUTE_STATISTICS_H_

#include <curl/curl.h>
#include <uv.h>

typedef struct {
    char *memory;
    size_t size;
} memory_t;

typedef struct {
    memory_t *buffer;
    char ticker_string[16];
    CURL *ez;
    uv_work_t job;
} private_data_t;

typedef struct {
    char avg_move_10_up[16];
    char avg_move_10_down[16];
    char change[16];
    char record_count[16];
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
    char response_ticker[8];
} sign_diff_pct;

#define EZ_POOL_SIZE 4
typedef struct {
    CURL *ez_pool[EZ_POOL_SIZE];
    CURLM *curl_multi;
} curl_multi_ez_t;

void build_sign_diff_print_json(char sign_diff_json[],
    sign_diff_pct *sign_diff_values);
void build_sign_diff_print_string(char sign_diff_print[],
    sign_diff_pct *sign_diff_values);
CURL *create_and_init_curl(void);
int16_t get_adj_close_and_changes(char *response_text, double *changes, int64_t *daily_volume);
void get_sigma_data(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values);
void get_timestamps(char timestamps[][12]);
int get_title(const char *response_text, char *title);
char *prime_crumb();
void process_tickers(char *ticker_string, curl_multi_ez_t *curl_multi_ez,
    char timestamps[][12]);
void reset_private_data(const private_data_t *private_data);
void run_stats(const char *ticker_string, sign_diff_pct *sign_diff_values,
    const CURL *curl, char timestamps[][12]);

#endif  // COMPUTE_STATISTICS_H_
