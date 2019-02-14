#ifndef COMPUTE_STATISTICS_H
#define COMPUTE_STATISTICS_H

#include <curl/curl.h>

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

void build_sign_diff_print_string(char sign_diff_print[], sign_diff_pct *sign_diff_values);
void get_timestamps(char timestamps[][12]);
void process_tickers(char *ticker_string, CURL *curl, char timestamps[][12]);
void run_stats(const char *ticker_string, sign_diff_pct *sign_diff_values, CURL *curl, char timestamps[][12]);

#endif