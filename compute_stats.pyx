import sys
import requests

from libc.stdio cimport fflush
from libc.stdio cimport fgets
from libc.stdio cimport printf
from libc.stdio cimport sprintf
from libc.stdio cimport stdin as cstdin
from libc.stdio cimport stdout as cstdout
from libc.stdlib cimport atof
from libc.stdlib cimport free
from libc.stdlib cimport malloc
from libc.stdlib cimport qsort
from libc.string cimport memset
from libc.string cimport strcmp
from libc.string cimport strlen
from libc.string cimport strstr
from libc.string cimport strcpy
from libc.string cimport strncpy
from libc.string cimport strtok
from libc.string cimport strsep
from libc.time cimport localtime
from libc.time cimport mktime
from libc.time cimport time
from libc.time cimport time_t
from libc.time cimport tm
from posix.time cimport CLOCK_MONOTONIC
from posix.time cimport clock_gettime
from posix.time cimport timespec
from posix.unistd cimport usleep


cdef extern from "ctype.h":
    int toupper(int c)

cdef extern from "gsl/gsl_statistics_double.h":
    double gsl_stats_mean(const double data[], const size_t stride, const size_t n)
    double gsl_stats_sd(const double data[], const size_t stride, const size_t n)
    double gsl_stats_correlation(const double data1[], const size_t stride1,const double data2[], const size_t stride2, const size_t n);

ctypedef struct changes_tuple:
    double change_0
    double change_plus_one

cdef int compare_changes_tuples(const void *a, const void *b) nogil:
    cdef changes_tuple a_val = (<const changes_tuple*>a)[0]
    cdef changes_tuple b_val = (<const changes_tuple*>b)[0]

    if a_val.change_0 < b_val.change_0:
        return 1
    if a_val.change_0 > b_val.change_0:
        return -1
    return 0

ctypedef struct sign_diff_pct:
    char avg_move_10_up[16]
    char avg_move_10_down[16]
    char change[16]
    char record_count[8]
    char self_correlation[16]
    char sigma[16]
    char sigma_change[16]
    char stdev_10_up[16]
    char stdev_10_down[16]
    char sign_diff_pct_10_up[16]
    char sign_diff_pct_20_up[16]
    char sign_diff_pct_10_down[16]
    char sign_diff_pct_20_down[16]
    char title[128]

# Looks like there is an issue here for some cases with unicode characters.
cdef void get_crumb(const char *response_text, char *crumb):
    cdef const char *crumbstore = strstr(response_text, "CrumbStore")
    cdef const char *colon_quote = strstr(crumbstore, ":\"")
    cdef const char *end_quote = strstr(&colon_quote[2], "\"")
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) - strlen(end_quote))
    return

cdef void get_timestamps(char timestamps[][12]):
    memset(timestamps[0], 0, 12)
    memset(timestamps[1], 0, 12)    
    cdef time_t now = time(NULL)
    cdef tm *now_tm = localtime(&now)
    now_tm.tm_sec = 0
    now_tm.tm_min = 0
    now_tm.tm_hour = 0
    cdef time_t today_time = mktime(now_tm)
    cdef time_t manana = today_time + 86400
    cdef time_t ago_366_days = today_time - 31622400
    sprintf(timestamps[0], "%ld", manana)
    sprintf(timestamps[1], "%ld", ago_366_days)
    return

cdef void get_title(const char *response_text, char *title):
    """Extracts the company title string from the response text."""
    cdef const char* title_start = strstr(response_text, "<title>")
    cdef const char* pipe_start = strstr(title_start, "|")
    cdef const char* hyphen_end = strstr(&pipe_start[2], "-")
    cdef size_t diff = strlen(&pipe_start[2]) - strlen(hyphen_end)
    strncpy(title, &pipe_start[2], diff)
    return

cdef int get_adj_close_and_changes(char *response_text, double *changes):
    """Extracts prices from text and computes daily changes."""
    cdef int i = 0
    cdef int j
    cdef double adj_close
    cdef double last_adj_close
    cdef char line[512]
    cdef char adj_close_str[128]
    cdef char *last_column

    cdef char *token = strtok(response_text, "\n")
    while token:
        if i:
            memset(line, 0, 512)
            memset(adj_close_str, 0, 128)
            strcpy(line, token)

            for j in range(5):
                line = strstr(&line[1], ",")
            last_column = strstr(&line[1], ",")
            strncpy(adj_close_str, &line[1], strlen(&line[1]) - strlen(last_column))
            if strcmp(adj_close_str, "null") == 0:
                return 0

            adj_close = atof(adj_close_str)
            if i > 1:
                changes[i-2] = (adj_close - last_adj_close)/last_adj_close
            last_adj_close = adj_close

        token = strtok(NULL, "\n")
        i += 1

    return i - 2

cdef void compute_sign_diff_pct(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values):
    """Computes sign-diffs for up and down 10 and 20 blocks."""
    cdef int i
    cdef double changes_minus_one[changes_length - 2]
    cdef double changes_0[changes_length - 2]
    cdef changes_tuple changes_tuples[changes_length - 2]
    cdef double np_avg_10_up[10]
    cdef double np_avg_10_down[10]

    for i in range(changes_length - 2):
        changes_minus_one[i] = changes_daily[i]
        changes_0[i] = changes_daily[i+1]
        changes_tuples[i].change_0 = changes_daily[i]
        changes_tuples[i].change_plus_one = changes_daily[i+1]

    cdef double self_correlation = gsl_stats_correlation(changes_minus_one, 1, changes_0, 1, changes_length - 2)
    qsort(changes_tuples, changes_length - 2, sizeof(changes_tuple), compare_changes_tuples)

    cdef int pct_sum_10_up = 0
    cdef int pct_sum_10_down = 0
    cdef int pct_sum_20_up = 0
    cdef int pct_sum_20_down = 0
    cdef double product_up
    cdef double product_down

    for i in range(20):
        product_up = changes_tuples[i].change_0 * changes_tuples[i].change_plus_one
        product_down = changes_tuples[changes_length - 22 + i].change_0 * changes_tuples[changes_length - 22 + i].change_plus_one

        if i < 10:
            np_avg_10_up[i] = changes_tuples[i].change_plus_one
            np_avg_10_down[i] = changes_tuples[changes_length - 12 + i].change_plus_one

        if product_up and product_up < 0:
            pct_sum_20_up += 1
            if i < 10:
                pct_sum_10_up += 1

        if product_down and product_down < 0:
            pct_sum_20_down += 1
            if i > 9:
                pct_sum_10_down += 1

    cdef double avg_10_up = gsl_stats_mean(np_avg_10_up, 1, 10)
    cdef double stdev_10_up = gsl_stats_sd(np_avg_10_up, 1, 10)

    cdef double avg_10_down = gsl_stats_mean(np_avg_10_down, 1, 10)
    cdef double stdev_10_down = gsl_stats_sd(np_avg_10_down, 1, 10)

    sprintf(sign_diff_values.avg_move_10_up, "%.4f%%", avg_10_up * 100)
    sprintf(sign_diff_values.avg_move_10_down, "%.4f%%", avg_10_down * 100)
    sprintf(sign_diff_values.stdev_10_up, "%.4f%%", stdev_10_up * 100)
    sprintf(sign_diff_values.stdev_10_down, "%.4f%%", stdev_10_down * 100)
    sprintf(sign_diff_values.self_correlation, "%.3f%%", self_correlation * 100)
    sprintf(sign_diff_values.sign_diff_pct_10_up, "%.1f%%", pct_sum_10_up * 10.0)
    sprintf(sign_diff_values.sign_diff_pct_10_down, "%.1f%%", pct_sum_10_down * 10.0)
    sprintf(sign_diff_values.sign_diff_pct_20_up, "%.1f%%", pct_sum_20_up * 5.0)
    sprintf(sign_diff_values.sign_diff_pct_20_down, "%.1f%%", pct_sum_20_down * 5.0)

cdef void get_sigma_data(const double *changes_daily, const int changes_length, sign_diff_pct *sign_diff_values):
    """Computes standard change/standard deviation and constructs dict object."""
    # st = time.time_ns()
    compute_sign_diff_pct(changes_daily, changes_length, sign_diff_values)
    # en = time.time_ns()
    # print('ran compute_sign_diff_pct in %d ns' % (en - st))

    cdef double stdev  = gsl_stats_sd(changes_daily, 1, (changes_length - 1))
    cdef double sigma_change = changes_daily[changes_length - 1]/stdev

    sprintf(sign_diff_values.change, "%.3f%%", changes_daily[changes_length - 1] * 100)
    sprintf(sign_diff_values.sigma, "%.3f%%", stdev * 100)
    sprintf(sign_diff_values.sigma_change, "%.3f", sigma_change)
    sprintf(sign_diff_values.record_count, "%d", changes_length)

cdef void process_ticker(char *ticker, char timestamps[][12]):
    """Makes requests to get crumb and data and call stats computation."""
    cdef timespec start
    cdef timespec end   

    cdef char url[128]
    memset(url, 0, 128)
    sprintf(url, "https://finance.yahoo.com/quote/%s/history?p=%s", ticker, ticker)
    printf("url = %s\n", url)

    response = requests.get(url.decode('UTF-8'))

    cdef char crumb[128]
    memset(crumb, 0, 128)

    resp_encode = response.text.encode('UTF-8')
    cdef const char* response_text_char = resp_encode
    
    cdef sign_diff_pct sign_diff_values

    memset(sign_diff_values.title, 0, 128)
    get_title(response_text_char, sign_diff_values.title)

    get_crumb(response_text_char, crumb)

    cdef char download_url[256]
    memset(download_url, 0, 256)

    sprintf(download_url, "https://query1.finance.yahoo.com/v7/finance/download/%s?period1=%s&period2=%s&interval=1d&events=history&crumb=%s", ticker, timestamps[1], timestamps[0], crumb)
    printf("download_url = %s\n", download_url)

    download_response = requests.get(download_url.decode('UTF-8'), cookies=response.cookies)

    cdef double changes_daily[512]

    dl_resp_char = download_response.text.encode('UTF-8')
    cdef char *download_response_char = dl_resp_char

    clock_gettime(CLOCK_MONOTONIC, &start)
    cdef int changes_length = get_adj_close_and_changes(download_response_char, changes_daily)
    clock_gettime(CLOCK_MONOTONIC, &end)
    printf("ran get adj_close_and_changes in %ld ns.\n", end.tv_nsec - start.tv_nsec)

    if not changes_length:
        return

    get_sigma_data(changes_daily, changes_length, &sign_diff_values)

    printf("===============================\n")
    printf("  \"avg_move_10_down\": %s\n", sign_diff_values.avg_move_10_down)
    printf("  \"avg_move_10_up\": %s\n", sign_diff_values.avg_move_10_up)
    printf("  \"title\": \"%s\"\n", sign_diff_values.title)
    printf("  \"change\": %s\n", sign_diff_values.change)
    printf("  \"record_count\": %s\n", sign_diff_values.record_count)
    printf("  \"self_correlation\": %s\n", sign_diff_values.self_correlation)
    printf("  \"sigma\": %s\n", sign_diff_values.sigma)
    printf("  \"sigma_change\": %s\n", sign_diff_values.sigma_change)
    printf("  \"sign_diff_pct_10_down\": %s\n", sign_diff_values.sign_diff_pct_10_down)
    printf("  \"sign_diff_pct_10_up\": %s\n", sign_diff_values.sign_diff_pct_10_up)
    printf("  \"sign_diff_pct_20_down\": %s\n", sign_diff_values.sign_diff_pct_20_down)
    printf("  \"sign_diff_pct_20_up\": %s\n", sign_diff_values.sign_diff_pct_20_up)
    printf("  \"stdev_10_down\": %s\n", sign_diff_values.stdev_10_down)
    printf("  \"stdev_10_up\": %s\n", sign_diff_values.stdev_10_up)

cdef process_tickers(char *ticker_string, char timestamps[][12]):
    """Processes all of the input tickers by looping over the list."""
    cdef char *ticker = strsep(&ticker_string, " ")
    while ticker != NULL: 
        process_ticker(ticker, timestamps)
        ticker = strsep(&ticker_string, " ")
        if ticker != NULL:
            usleep(1500000)

def main():
    """The main routine and application entry point of this module."""
    cdef char timestamps[2][12]
    get_timestamps(timestamps)
    cdef timespec start
    cdef timespec end

    cdef int ticker_strlen
    cdef char ticker_string[128]
    cdef char ticker_string_strip[128]

    if len(sys.argv) < 2:
        while True:
            memset(ticker_string, 0, 128)
            memset(ticker_string_strip, 0, 128)
            printf("%s", "Enter ticker list: ")
            fflush(cstdout)
            fgets(ticker_string, 128, cstdin)
            ticker_strlen = strlen(ticker_string) - 1
            strncpy(ticker_string_strip, ticker_string, ticker_strlen)
            for i in range(ticker_strlen):
                ticker_string_strip[i] = toupper(ticker_string_strip[i])
            
            clock_gettime(CLOCK_MONOTONIC, &start)
            process_tickers(ticker_string_strip, timestamps)
            clock_gettime(CLOCK_MONOTONIC, &end)
            printf("processed in %.5f s\n", (<double>end.tv_sec + 1.0e-9*end.tv_nsec) - (<double>start.tv_sec + 1.0e-9*start.tv_nsec))
        return

    # need to fix this to C
    # ticker_list = [s.strip().upper() for s in sys.argv[1:]]
    # process_tickers(ticker_string, timestamps)

if __name__ == '__main__':
    main()
