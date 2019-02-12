// Compile with: gcc stats_fastcgi.c -o stats_fastcgi.fcgi -lfcgi -O3 -Wall -Wextra -pedantic -std=c11
#include <stdlib.h>
#include <fcgi_stdio.h>
#include "compute_statistics.h"

int main (void) {
    while (FCGI_Accept() >= 0) {
        char *query_string = getenv("QUERY_STRING");
        char *path_info = getenv("PATH_INFO");

        printf("Status: 200 OK\r\n");
        printf("Content-type: text/html\r\n\r\n");
        printf("<!doctype><html><body><pre>Hola mundo from fastcgi with lighttpd!\n");

        if (path_info != NULL) {
            printf("path_info = &quot;%s&quot;\n", path_info);
        }
        if (query_string != NULL) {
            printf("query_string = &quot;%s&quot;\n", query_string);
        }

        char *ts_string = get_ts();

        printf("ts_string = %s\n", ts_string);

        printf("</pre></body></html>\n");
    }

    return EXIT_SUCCESS;
}
