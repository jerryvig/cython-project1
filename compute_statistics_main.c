#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <sds.h>
#include <uv.h>
#include "compute_statistics.h"

#define INPUT_BUFFER_SIZE 128

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)
#define THREAD_POOL_SIZE 4

static uv_loop_t *loop;
static uv_signal_t sigint_watcher;
static uv_fs_t stdin_watcher;
static uv_fs_t stdout_watcher;
static uv_timer_t timeout;

static char ticker_buffer[INPUT_BUFFER_SIZE];
static const char *prompt = "Enter ticker list: ";
static char *crumb;
static int16_t stdin_len;
static char timestamps[2][12];
static struct timespec start;
static struct timespec end;
static curl_multi_ez_t curl_multi_ez;

static size_t transfers;
static size_t completed_transfers;

/* static struct timespec startup_time_start;
static struct timespec startup_time_end; */

static uv_work_t jobs[THREAD_POOL_SIZE];
static uint8_t job_index = 0;

typedef struct curl_context_s {
    uv_poll_t poll_handle;
    curl_socket_t sockfd;
} curl_context_t;

typedef struct {
    char **strings;
    int16_t size;
} string_list_t;

static void string_list_add(string_list_t *string_list, char *string) {
    string_list->size++;
    string_list->strings = (char**)realloc(string_list->strings, string_list->size * sizeof(char*));
    string_list->strings[string_list->size - 1] = string;
}

static string_list_t ticker_list;

static void init_watchers();

static void cleanup_curl_multi_ez() {
    for (register int8_t i = 0; i < EZ_POOL_SIZE; ++i) {
        memory_t *private_data;
        CURL *ez = curl_multi_ez.ez_pool[i];
        curl_easy_getinfo(ez, CURLINFO_PRIVATE, &private_data);
        if (private_data != NULL) {
            free(private_data->memory);
        }
        free(private_data);
    }
    curl_multi_cleanup(curl_multi_ez.curl_multi);
    curl_global_cleanup();
}

static void on_sigint(uv_signal_t *sig, int signum) {
    uv_signal_stop(sig);
    uv_close((uv_handle_t*)sig, NULL);
    cleanup_curl_multi_ez();

    if (ticker_list.strings != NULL) {
        free(ticker_list.strings);
    }

    uv_kill(getpid(), SIGTERM);
}

static void add_download(const char *ticker, size_t num, CURL *ez) {
    const size_t ez_pool_index = num % EZ_POOL_SIZE;
    if (ez == NULL) {
        // do round-robin assignment if no ez handle is provided.
        ez = curl_multi_ez.ez_pool[ez_pool_index];
    }

    char download_url[256] = {'\0'};
    // this format string can be pre-constructed to include the timestamps and crumb.
    sprintf(download_url, "https://query1.finance.yahoo.com/v7/finance/download/%s?period1=%s&period2=%s&interval=1d&events=history&crumb=%s", ticker, timestamps[1], timestamps[0], crumb);
    curl_easy_setopt(ez, CURLOPT_URL, download_url);

    curl_multi_add_handle(curl_multi_ez.curl_multi, ez);
}

static sds sds_ticker_string;
static void start_transfers(const char *ticker_string) {
    sdsfree(sds_ticker_string);
    sds_ticker_string = sdsnew(ticker_string);
    sdstrim(sds_ticker_string, " \n");
    sdstoupper(sds_ticker_string);

    if (ticker_list.strings != NULL) {
        free(ticker_list.strings);
    }

    ticker_list.size = 0;
    ticker_list.strings = (char**)malloc(sizeof(char*));

    char *ticker;
    do {
        ticker = strsep(&sds_ticker_string, " ");
        if (ticker != NULL) {
            string_list_add(&ticker_list, ticker);
        }
    } while (ticker!= NULL);

    completed_transfers = 0;
    for (transfers = 0; (transfers < EZ_POOL_SIZE && transfers < (size_t)ticker_list.size); ++transfers) {
        add_download(ticker_list.strings[transfers], transfers, NULL);
    }
}

static void on_stdin_read(uv_fs_t *read_req) {
    if (stdin_watcher.result > 0) {
        stdin_len = strlen(ticker_buffer);
        ticker_buffer[stdin_len - 1] = '\0';

        if (!(stdin_len - 1)) {
            printf("Got empty ticker string...\n");
        } else {
            clock_gettime(CLOCK_MONOTONIC, &start);
            start_transfers(ticker_buffer);
        }
    } else if (stdin_watcher.result < 0) {
        fprintf(stderr, "error opening stdin...\n");
    }
}

static void init_watchers(void) {
    memset(ticker_buffer, 0, INPUT_BUFFER_SIZE);
    if (ticker_list.strings != NULL) {
        free(ticker_list.strings);
        ticker_list.strings = NULL;
    }

    uv_buf_t stdin_buf = uv_buf_init(ticker_buffer, INPUT_BUFFER_SIZE);
    uv_buf_t stdout_buf = uv_buf_init(prompt, strlen(prompt));
    uv_fs_write(loop, &stdout_watcher, STDOUT_FILENO, &stdout_buf, 1, -1, NULL);
    uv_fs_read(loop, &stdin_watcher, STDIN_FILENO, &stdin_buf, 1, -1, on_stdin_read);
}

static curl_context_t* create_curl_context(curl_socket_t sockfd) {
    curl_context_t *context = (curl_context_t*)malloc(sizeof(curl_context_t));
    context->sockfd = sockfd;

    int r = uv_poll_init_socket(loop, &context->poll_handle, sockfd);
    if (r) {
        fprintf(stderr, "Failed to initialize poller on socket.\n");
    }
    assert(r == 0);
    context->poll_handle.data = context;
    return context;
}

void reset_private_data(const private_data_t *private_data) {
    free(private_data->buffer->memory);
    private_data->buffer->memory = (char*)malloc(1);
    private_data->buffer->size = 0;
    memset(private_data->ticker_string, 0, sizeof private_data->ticker_string);
}

static void after_work(uv_work_t *job, int status) {
    if (!status) {
        private_data_t *private_data = (private_data_t*)job->data;
        if (private_data->buffer && transfers < (size_t)ticker_list.size) {
            reset_private_data(private_data);
            add_download(ticker_list.strings[transfers], transfers, private_data->ez);
            transfers++;
        } else if (transfers >= (size_t)ticker_list.size) {
            reset_private_data(private_data);
        }

        // check completed_transfers count to confirm all transfers are done.
        completed_transfers++;
        if (completed_transfers == (size_t)ticker_list.size) {
            // getting end clock-time and printing total transfer time.
            clock_gettime(CLOCK_MONOTONIC, &end);
            fprintf(stderr, "proc'ed in %.6f s\n",
                   ((double)end.tv_sec + 1.0e-9 * end.tv_nsec) -
                   ((double)start.tv_sec + 1.0e-9 * start.tv_nsec));

            init_watchers();
        }
        // free(job);
        job->data = NULL;
    }
}

static void do_work(uv_work_t *job) {
    private_data_t *private_data = (private_data_t*)job->data;

    char first_nine[10] = {'\0'};
    strncpy(first_nine, private_data->buffer->memory, 9);
    if (strcmp(first_nine, "Date,Open") != 0) {
        printf("Response does not contain historical data...\n");
        return;
    }

    double changes_daily[512];
    const int16_t changes_length = get_adj_close_and_changes(private_data->buffer->memory, changes_daily);

    if (!changes_length) {
        printf("Failed to parse adj_close and changes data from response.\n");
        return;
    }

    sign_diff_pct sign_diff_values;
    strcpy(sign_diff_values.response_ticker, private_data->ticker_string);
    strcpy(sign_diff_values.title, "N/A");
    get_sigma_data(changes_daily, changes_length, &sign_diff_values);

    char sign_diff_print[512] = {'\0'};
    build_sign_diff_print_string(sign_diff_print, &sign_diff_values);
    fprintf(stderr, "%s", sign_diff_print);
}

static void on_poll_handle_close(uv_handle_t *handle) {
    free(handle);
}

static void destroy_curl_context(curl_context_t *context) {
    uv_close((uv_handle_t*)&context->poll_handle, on_poll_handle_close);
}

static void check_multi_info(void) {
    char *done_url;
    CURLMsg *message;
    int pending;

    CURL *ez;
    private_data_t *private_data;

    while ((message = curl_multi_info_read(curl_multi_ez.curl_multi, &pending))) {
        if (message->msg == CURLMSG_DONE) {
            ez = message->easy_handle;
            curl_easy_getinfo(ez, CURLINFO_EFFECTIVE_URL, &done_url);
            curl_easy_getinfo(ez, CURLINFO_PRIVATE, &private_data);
            fprintf(stderr, "Finished fetching data for %s\n", done_url);

            if (private_data) {
                // Queue up the worker thread to process the data.
                // we could maybe do this without the malloc() using a pool of uv_work_t's.
                //uv_work_t *job = (uv_work_t*)malloc(sizeof(uv_work_t));
                //The problem here is that the job indexed this way could be in use,
                //so you may be queueing up a uv_work_t object that is already on the queue.
                uv_work_t job = jobs[job_index % THREAD_POOL_SIZE];
                job.data = (void*)private_data;
                uv_queue_work(loop, &job, do_work, after_work);
                job_index++;
            }

            // remove the cURL ez handle from the cURL multi handle.
            curl_multi_remove_handle(curl_multi_ez.curl_multi, ez);
        } else {
            fprintf(stderr, "CURL message default.\n");
        }
    }
}

static void on_timeout(uv_timer_t *req) {
    int running_handles;
    curl_multi_socket_action(curl_multi_ez.curl_multi, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_multi_info();
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
    return 0;
}

static void curl_perform(uv_poll_t *poll_handle, int status, int events) {
    int running_handles;
    int flags = 0;

    if (status < 0) {
        flags = CURL_CSELECT_ERR;
    }

    if (events & UV_READABLE) {
        flags |= CURL_CSELECT_IN;
    }
    if (events & UV_WRITABLE) {
        flags |= CURL_CSELECT_OUT;
    }

    curl_context_t *context = (curl_context_t*)poll_handle->data;
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

            //start polling with uv.
            uv_poll_start(&curl_context->poll_handle, events, curl_perform);
            break;
        case CURL_POLL_REMOVE:
            if (socketp) {
                uv_poll_stop(&((curl_context_t*)socketp)->poll_handle);
                destroy_curl_context((curl_context_t*)socketp);
                curl_multi_assign(curl_multi_ez.curl_multi, sock, NULL);
            }
            break;
        default:
            fprintf(stderr, "ABOUT TO CALL ABORT() SYSTEM CALL\n");
            abort();
    }
    return 0;
}

static CURLM *create_and_init_curl_multi() {
    CURLM *multi_handle = curl_multi_init();
    curl_multi_setopt(multi_handle, CURLMOPT_MAXCONNECTS, (long)EZ_POOL_SIZE);
    curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, start_timeout);
    return multi_handle;
}

static void create_and_init_multi_ez() {
    curl_multi_ez.curl_multi = create_and_init_curl_multi();
    for (register int8_t i = 0; i < EZ_POOL_SIZE; ++i) {
        curl_multi_ez.ez_pool[i] = create_and_init_curl();
    }
}

void *get_timestamps_thread_proc(void *args) {
    get_timestamps(timestamps);
}

void *prime_crumb_thread_proc(void *args) {
    curl_multi_ez_t *multi_ez = (curl_multi_ez_t*)args;
    crumb = prime_crumb();
}

int main(void) {
    //clock_gettime(CLOCK_MONOTONIC, &startup_time_start);
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        fprintf(stderr, "Failed to initialize cURL...\n");
        return EXIT_FAILURE;
    }
    putenv("UV_THREADPOOL_SIZE=" STRINGIFY(THREAD_POOL_SIZE));

    loop = uv_default_loop();

    uv_timer_init(loop, &timeout);

    create_and_init_multi_ez();

    pthread_t crumb_thread;
    pthread_t timestamps_thread;
    pthread_create(&crumb_thread, NULL, prime_crumb_thread_proc, (void*)&curl_multi_ez);
    pthread_create(&timestamps_thread, NULL, get_timestamps_thread_proc, NULL);

    uv_signal_t sigint_watcher;
    uv_signal_init(loop, &sigint_watcher);
    uv_signal_start(&sigint_watcher, on_sigint, SIGINT);

    pthread_join(timestamps_thread, NULL);
    pthread_join(crumb_thread, NULL);

    //clock_gettime(CLOCK_MONOTONIC, &startup_time_end);
    //fprintf(stderr, "startup time = %.6f s\n",
    //        ((double)end.tv_sec + 1.0e-9 * startup_time_end.tv_nsec) -
    //        ((double)start.tv_sec + 1.0e-9 * startup_time_start.tv_nsec));

    init_watchers();

    uv_run(loop, UV_RUN_DEFAULT);

    cleanup_curl_multi_ez();
    return EXIT_SUCCESS;
}
