#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
#include <uv.h>
#include "compute_statistics.h"

#define BUFFER_SIZE 128

/* int main(void) {
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
} */

static uv_loop_t *loop;
static uv_signal_t sigint_watcher;
static uv_fs_t stdin_watcher;
static uv_fs_t stdout_watcher;
static char ticker_buffer[BUFFER_SIZE];
static char *prompt = "Enter ticker list: ";
static void init_watchers();
static const CURL *curl;
static char timestamps[2][12];
static struct timespec start;
static struct timespec end;

static void on_sigint(uv_signal_t *sig, int signum) {
    uv_signal_stop(sig);
    uv_close((uv_handle_t*)sig, NULL);

    curl_easy_cleanup((CURL*)curl);

    uv_kill(getpid(), SIGTERM);
}

static void on_stdin_read(uv_fs_t *read_req) {
    if (stdin_watcher.result > 0) {
        size_t stdin_len = strlen(ticker_buffer);
        ticker_buffer[stdin_len - 1] = '\0';

        if (!(stdin_len - 1)) {
            printf("Got empty ticker string...\n");
        } else {
            //Call into the real processing here.
        }
        init_watchers();
    } else if (stdin_watcher.result < 0) {
        fprintf(stderr, "error opening stdin.\n");
    }
}

static void init_watchers() {
    memset(ticker_buffer, 0, BUFFER_SIZE);
    uv_buf_t stdin_buf = uv_buf_init(ticker_buffer, BUFFER_SIZE);
    uv_buf_t stdout_buf = uv_buf_init(prompt, strlen(prompt));
    uv_fs_write(loop, &stdout_watcher, STDOUT_FILENO, &stdout_buf, 1, -1, NULL);
    uv_fs_read(loop, &stdin_watcher, STDIN_FILENO, &stdin_buf, 1, -1, on_stdin_read);
}

int main(void) {
    curl = create_and_init_curl();

    get_timestamps(timestamps);

    loop = uv_default_loop();

    uv_signal_t sigint_watcher;
    uv_signal_init(loop, &sigint_watcher);
    uv_signal_start(&sigint_watcher, on_sigint, SIGINT);

    init_watchers();

    uv_run(loop, UV_RUN_DEFAULT);

    curl_easy_cleanup((CURL*)curl);
    return EXIT_SUCCESS;
}
