// Compile with: gcc stats_fastcgi.c compute_statistics.c -o stats_fastcgi.fcgi -lfcgi -lcurl -lgsl -lgslcblas -O3 -Wall -Wextra -pedantic -std=c11
#include <fcgi_stdio.h>
#include <stdlib.h>
#include "compute_statistics.h"

int main (void) {
    while (FCGI_Accept() >= 0) {
        char *query_string = getenv("QUERY_STRING");
        char *path_info = getenv("PATH_INFO");
        sign_diff_pct sign_diff_values;
        run_stats("HON", &sign_diff_values);
        char sign_diff_print[512];
        build_sign_diff_print_string(sign_diff_print, &sign_diff_values);

        printf("Status: 200 OK\r\n");
        printf("Content-type: text/html\r\n\r\n");
        printf("<!doctype><html><body><pre>Hola mundo from fastcgi with lighttpd!\n");

        if (path_info != NULL) {
            printf("path_info = &quot;%s&quot;\n", path_info);
        }
        if (query_string != NULL) {
            printf("query_string = &quot;%s&quot;\n", query_string);
        }
        printf("%s", sign_diff_print);

        printf("</pre></body></html>\n");
    }

    return EXIT_SUCCESS;
}
