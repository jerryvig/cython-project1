#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
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
        fgets(ticker_string, 128, stdin);
        
        ticker_strlen = strlen(ticker_string) - 1;
        if (!ticker_strlen) {
            printf("Got empty ticker string....\n");
            continue;
        }
        ticker_string[ticker_strlen] = NULL;

        clock_gettime(CLOCK_MONOTONIC, &start);
        process_tickers(ticker_string, curl, timestamps);
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("processed in %.5f s\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec));
    }

    curl_easy_cleanup(curl);
    return EXIT_SUCCESS;
}
