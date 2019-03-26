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

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define THREAD_POOL_SIZE 4

static uv_loop_t *loop;
static uv_signal_t sigint_watcher;
static uv_fs_t stdin_watcher;
static uv_fs_t stdout_watcher;
static uv_timer_t timeout;

static char ticker_buffer[BUFFER_SIZE];
static const char *prompt = "Enter ticker list: ";
static size_t stdin_len;
static char timestamps[2][12];
static struct timespec start;
static struct timespec end;
static curl_multi_ez_t curl_multi_ez;

typedef struct curl_context_s {
    uv_poll_t poll_handle;
    curl_socket_t sockfd;
} curl_context_t;

static void init_watchers();

static void cleanup_curl_multi_ez() {
    for (register int8_t i = 0; i < EZ_POOL_SIZE; ++i) {
        curl_easy_cleanup(curl_multi_ez.ez_pool[i]);
    }
    curl_multi_cleanup(curl_multi_ez.curl_multi);
}

static void on_sigint(uv_signal_t *sig, int signum) {
    uv_signal_stop(sig);
    uv_close((uv_handle_t*)sig, NULL);
    cleanup_curl_multi_ez();
    uv_kill(getpid(), SIGTERM);
}

static void on_stdin_read(uv_fs_t *read_req) {
    if (stdin_watcher.result > 0) {
        stdin_len = strlen(ticker_buffer);
        ticker_buffer[stdin_len - 1] = '\0';

        if (!(stdin_len - 1)) {
            printf("Got empty ticker string...\n");
        } else {
            clock_gettime(CLOCK_MONOTONIC, &start);
            process_tickers(ticker_buffer, &curl_multi_ez, timestamps);
            clock_gettime(CLOCK_MONOTONIC, &end);

            printf("proc'ed in %.6f s\n",
                   ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
                   ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));
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

static void on_timeout(uv_timer_t *req) {
    int running_handles;
    curl_multi_socket_action(curl_multi_ez.curl_multi, CURL_SOCKET_TIMEOUT, 0, &running_handles);
}

static int start_timeout(CURLM *curl_multi, long timeout_ms, void *userp) {
    if (timeout_ms < 0) {
        uv_timer_stop(&timeout);
    } else {
        if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        uv_timer_start(&timeout, on_timeout, timeout_ms, 0);
    }
}

static curl_context_t* create_curl_context(curl_socket_t sockfd) {
    curl_context_t *context = (curl_context_t*)malloc(sizeof(curl_context_t));
    context->sockfd = sockfd;

    int r = uv_poll_init_socket(loop, &context->poll_handle, sockfd);
    if (r) {
        fprintf(stderr, "Failed to initialize poller on socket.\n");
    }
    context->poll_handle.data = context;
    return context;
}

static void destroy_curl_context(curl_context_t *context) {
    uv_close((uv_handle_t*)&context->poll_handle, NULL);
    free(context);
}

static void check_multi_info(void) {
    char *done_url;
    CURLMsg *message;
    int pending;

    CURL *ez;
    memory_t *buffer;

    while ((message = curl_multi_info_read(curl_multi_ez.curl_multi, &pending))) {
        switch(message->msg) {
            case CURLMSG_DONE:
                ez = message->easy_handle;
                curl_easy_getinfo(ez, CURLINFO_EFFECTIVE_URL, &done_url);
                curl_easy_getinfo(ez, CURLINFO_PRIVATE, &buffer);
                fprintf(stderr, "Finished fetching data for %s\n", done_url);

                break;
            default:
                fprintf(stderr, "CURL message default.\n");
                break;
        }
    }
}

static void curl_perform(uv_poll_t *poll_handle, int status, int events) {
    int running_handles;
    int flags = 0;
    curl_context_t *context;

    if (status < 0) {
        flags = CURL_CSELECT_ERR;
    }

    if (events & UV_READABLE) {
        flags |= CURL_CSELECT_IN;
    }
    if (events & UV_WRITABLE) {
        flags |= CURL_CSELECT_OUT;
    }

    context = (curl_context_t*)poll_handle->data;
    curl_multi_socket_action(curl_multi_ez.curl_multi, context->sockfd, flags, &running_handles);
    check_multi_info();
}

static int handle_socket(CURL *ez, curl_socket_t sock, int action, void *userp, void *socketp) {
    curl_context_t *curl_context;
    int events = 0;

    switch(action) {
        case CURL_POLL_IN:
        case CURL_POLL_OUT:
        case CURL_POLL_INOUT:
            curl_context = socketp ? (curl_context_t*)socketp : create_curl_context(sock);
            curl_multi_assign(curl_multi_ez.curl_multi, sock, (void*)curl_context);

            if (action != CURL_POLL_IN) {
                events |= UV_WRITABLE;
            }
            if (action != CURL_POLL_OUT) {
                events |= UV_READABLE;
            }

            uv_poll_start(&curl_context->poll_handle, events, curl_perform);
            break;
        case CURL_POLL_REMOVE:
            if (socketp) {
            }
    }
}

static CURLM *create_and_init_curl_multi() {
    CURLM *multi_handle = curl_multi_init();
    curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout);
    return multi_handle;
}

static void create_and_init_multi_ez() {
    curl_multi_ez.curl_multi = create_and_init_curl_multi();
    for (int8_t i = 0; i < EZ_POOL_SIZE; ++i) {
        curl_multi_ez.ez_pool[i] = create_and_init_curl();

        memory_t *buffer = (memory_t*)malloc(sizeof(memory_t));
        buffer->memory = (char*)malloc(1);
        buffer->size = 0;

        // curl_easy_setopt(curl_multi_ez.ez_pool[i], CURLOPT_WRITEDATA, (void*)buffer);
        curl_easy_setopt(curl_multi_ez.ez_pool[i], CURLOPT_PRIVATE, buffer);
    }
}

int main(void) {
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        fprintf(stderr, "Failed to initialize cURL.\n");
        return EXIT_FAILURE;
    }

    putenv("UV_THREADPOOL_SIZE=" STRINGIFY(THREAD_POOL_SIZE));

    loop = uv_default_loop();

    uv_timer_init(loop, &timeout);

    create_and_init_multi_ez();

    get_timestamps(timestamps);

    uv_signal_t sigint_watcher;
    uv_signal_init(loop, &sigint_watcher);
    uv_signal_start(&sigint_watcher, on_sigint, SIGINT);

    prime_crumb(&curl_multi_ez);

    init_watchers();

    uv_run(loop, UV_RUN_DEFAULT);

    cleanup_curl_multi_ez();
    return EXIT_SUCCESS;
}
