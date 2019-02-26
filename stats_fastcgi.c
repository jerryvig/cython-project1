// Compile with: gcc stats_fastcgi.c compute_statistics.c -o stats_fastcgi.fcgi -lfcgi -lcurl -lgsl -lgslcblas -O3 -Wall -Wextra -pedantic -std=c11
#include <fcgi_stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "compute_statistics.h"

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
