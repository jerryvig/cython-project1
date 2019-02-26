#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "compute_statistics.h"

int main(void) {
    const CURL *curl = create_and_init_curl();

    char timestamps[2][12];
    get_timestamps(timestamps);
    struct timespec start;
    struct timespec end;

    int ticker_strlen;
    char ticker_string[128];
    while (1) {
        memset(ticker_string, 0, 128);
        printf("%s", "Enter ticker list: ");
        fflush(stdout);
        if (fgets(ticker_string, 128, stdin) == NULL) {
            continue;
        }

        ticker_strlen = strlen(ticker_string) - 1;
        if (!ticker_strlen) {
            printf("Got empty ticker string....\n");
            continue;
        }
        ticker_string[ticker_strlen] = 0;

        clock_gettime(CLOCK_MONOTONIC, &start);
        process_tickers(ticker_string, curl, timestamps);
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("processed in %.6f s\n",
            ((double)end.tv_sec + 1.0e-9*end.tv_nsec) -
            ((double)start.tv_sec + 1.0e-9*start.tv_nsec));
    }

    curl_easy_cleanup((CURL*)curl);
    return EXIT_SUCCESS;
}
