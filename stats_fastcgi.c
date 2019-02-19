// Compile with: gcc stats_fastcgi.c compute_statistics.c -o stats_fastcgi.fcgi -lfcgi -lcurl -lgsl -lgslcblas -O3 -Wall -Wextra -pedantic -std=c11
#include <fcgi_stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "compute_statistics.h"

void build_sign_diff_print_json(char sign_diff_json[], sign_diff_pct *sign_diff_values) {
    const int temp_strlen = 128;
    char temp_str[temp_strlen];
    memset(sign_diff_json, 0, 512);
    memset(temp_str, 0, temp_strlen);

    sprintf(temp_str, "{\"avg_move_10_down\":\"%s\",", sign_diff_values->avg_move_10_down );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"avg_move_10_up\":\"%s\",", sign_diff_values->avg_move_10_up);
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"title\":\"%s\",", sign_diff_values->title );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"resp_ticker\":\"%s\",", sign_diff_values->response_ticker );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"change\":\"%s\",", sign_diff_values->change );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"record_count\":%s,", sign_diff_values->record_count );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"self_correlation\":\"%s\",", sign_diff_values->self_correlation );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sigma\":\"%s\",", sign_diff_values->sigma );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sigma_change\":%s,", sign_diff_values->sigma_change );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sign_diff_pct_10_down\":\"%s\",", sign_diff_values->sign_diff_pct_10_down );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sign_diff_pct_10_up\":\"%s\",", sign_diff_values->sign_diff_pct_10_up );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sign_diff_pct_20_down\":\"%s\",", sign_diff_values->sign_diff_pct_20_down );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"sign_diff_pct_20_up\":\"%s\",", sign_diff_values->sign_diff_pct_20_up );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"stdev_10_down\":\"%s\",", sign_diff_values->stdev_10_down );
    strcat(sign_diff_json, temp_str);
    memset(temp_str, 0, temp_strlen);
    sprintf(temp_str, "\"stdev_10_up\":\"%s\"}", sign_diff_values->stdev_10_up );
    strcat(sign_diff_json, temp_str);
}

int main (void) {
    const CURL *curl = create_and_init_curl();

    char timestamps[2][12];
    get_timestamps(timestamps);

    while (FCGI_Accept() >= 0) {
        const char *query_string = getenv("QUERY_STRING");
        const char *path_info = getenv("PATH_INFO");

        sign_diff_pct sign_diff_values;
        if (path_info != NULL) {
            if (strlen(path_info) > 1) {
                run_stats(&path_info[1], &sign_diff_values, curl, timestamps);
            }
        }

        char sign_diff_json[512];
        build_sign_diff_print_json(sign_diff_json, &sign_diff_values);

        printf("Status: 200 OK\r\nContent-type: application/json\r\n\r\n");
        printf("%s", sign_diff_json);
    }

    curl_easy_cleanup(curl);
    return EXIT_SUCCESS;
}
