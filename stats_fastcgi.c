// Compile with: gcc stats_fastcgi.c compute_statistics.c -o stats_fastcgi.fcgi -lfcgi -lcurl -lgsl -lgslcblas -O3 -Wall -Wextra -pedantic -std=c11
#include <fcgi_stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "compute_statistics.h"

int main (void) {
    const CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

    while (FCGI_Accept() >= 0) {
        const char *query_string = getenv("QUERY_STRING");
        const char *path_info = getenv("PATH_INFO");

        sign_diff_pct sign_diff_values;
        if (path_info != NULL) {
            if (strlen(path_info) > 1) {
                run_stats(&path_info[1], &sign_diff_values, curl);
            }
        }
        char sign_diff_print[512];
        build_sign_diff_print_string(sign_diff_print, &sign_diff_values);

        printf("Status: 200 OK\r\nContent-type: text/html\r\n\r\n");
        printf("<!doctype html><html><head></head><body style=\"width:100%%;\"><div style=\"width:40%%;margin:0 auto;\"><pre>COMPUTE STATISTICS FastCGI\n");

        if (query_string != NULL && strlen(query_string) > 0) {
            printf("query_string = &quot;%s&quot;\n", query_string);
        }

        if (path_info != NULL) {
            printf("%s", sign_diff_print);
        }

        printf("</pre></div></body></html>");
    }

    curl_easy_cleanup(curl);
    return EXIT_SUCCESS;
}
