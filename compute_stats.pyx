from concurrent.futures import ThreadPoolExecutor
import datetime
from datetime import date
import ujson
import sys
import time
import numpy
import requests

from libc.stdio cimport printf
from libc.stdio cimport sprintf
from libc.stdlib cimport atof
from libc.stdlib cimport malloc
from libc.stdlib cimport free
from libc.string cimport memset
from libc.string cimport strcmp
from libc.string cimport strlen
from libc.string cimport strstr
from libc.string cimport strcpy
from libc.string cimport strncpy
from libc.string cimport strtok
from libc.time cimport localtime
from libc.time cimport mktime
from libc.time cimport time as ctime
from libc.time cimport time_t
from libc.time cimport tm

cdef extern from "gsl/gsl_statistics_double.h":
    double gsl_stats_sd(const double data[], const size_t stride, const size_t n)
    double gsl_stats_correlation(const double data1[], const size_t stride1,const double data2[], const size_t stride2, const size_t n);

# Looks like there is an issue here for some cases.
cdef void get_crumb(const char *response_text, char *crumb):
    cdef const char *crumbstore = strstr(response_text, "CrumbStore")
    cdef const char *colon_quote = strstr(crumbstore, ":\"")
    cdef const char *end_quote = strstr(&colon_quote[2], "\"")
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) - strlen(end_quote))
    return

cdef void get_timestamps(char timestamps[][12]):
    memset(timestamps[0], 0, 12)
    memset(timestamps[1], 0, 12)    
    cdef time_t now = ctime(NULL)
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
                changes[i-1] = (adj_close - last_adj_close)/last_adj_close
            last_adj_close = adj_close

        token = strtok(NULL, "\n")
        i += 1

    # printf("changes count = %d\n", (i-1))
    return i - 1

cdef compute_sign_diff_pct(const double *changes_daily, const int changes_length):
    """Computes sign-diffs for up and down 10 and 20 blocks."""
    cdef double changes_minus_one[changes_length - 2]
    cdef double changes_0[changes_length - 2]

    for i in range(changes_length - 2):
        changes_minus_one[i] = changes_daily[i]
        changes_0[i] = changes_daily[i+1]

    cdef double self_correlation = gsl_stats_correlation(changes_minus_one, 1, changes_0, 1, changes_length - 2)

    # You are trying to fix this up.
    #changes_tuples = numpy.column_stack([changes_minus_one, changes_0])
    #sorted_descending = changes_tuples[changes_tuples[:, 0].argsort()[::-1]]

    # UP
    # pct_sum_10_up = 0
    # pct_sum_20_up = 0
    # np_avg_10_up = numpy.zeros(10, dtype=float)
    # for i, ele in enumerate(sorted_descending[:20]):
    #     product = ele[0] * ele[1]
    #     if i < 10:
    #         np_avg_10_up[i] = ele[1]
    #     if product:
    #         is_diff = -0.5*numpy.sign(product) + 0.5
    #         if i < 10:
    #             pct_sum_10_up += is_diff
    #         pct_sum_20_up += is_diff
    # avg_10_up = numpy.average(np_avg_10_up)
    # stdev_10_up = numpy.std(np_avg_10_up, ddof=1)

    # # DOWN
    # pct_sum_10_down = 0
    # pct_sum_20_down = 0
    # np_avg_10_down = numpy.zeros(10, dtype=float)
    # for i, ele in enumerate(sorted_descending[-20:]):
    #     product = ele[0] * ele[1]
    #     if i > 9:
    #         np_avg_10_down[i-10] = ele[1]
    #     if product:
    #         is_diff = -0.5*numpy.sign(product) + 0.5
    #         if i > 9:
    #             pct_sum_10_down += is_diff
    #         pct_sum_20_down += is_diff
    # avg_10_down = numpy.average(np_avg_10_down)
    # stdev_10_down = numpy.std(np_avg_10_down, ddof=1)

    

    return {
        #'avg_move_10_up': str(round(avg_10_up * 100, 4)) + '%',
        #'avg_move_10_down': str(round(avg_10_down * 100, 4)) + '%',
        'self_correlation': str(round(self_correlation * 100, 3)) + '%',
        #'sign_diff_pct_10_up':  str(round(pct_sum_10_up * 10, 4)) + '%',
        #'sign_diff_pct_20_up':  str(round(pct_sum_20_up * 5, 4)) + '%',
        #'sign_diff_pct_10_down': str(round(pct_sum_10_down * 10, 4)) + '%',
        #'sign_diff_pct_20_down': str(round(pct_sum_20_down * 5, 4)) + '%',
        #'stdev_10_up': str(round(stdev_10_up * 100, 4)) + '%',
        #'stdev_10_down': str(round(stdev_10_down * 100, 4)) + '%'
    }

cdef get_sigma_data(const double *changes_daily, const int changes_length):
    """Computes standard change/standard deviation and constructs dict object."""
    sign_diff_dict = compute_sign_diff_pct(changes_daily, changes_length)
    # sign_diff_dict = {}

    stdev  = gsl_stats_sd(changes_daily, 1, (changes_length - 1))
    sigma_change = changes_daily[changes_length - 1]/stdev

    sigma_data = {
        #'avg_move_10_up': sign_diff_dict['avg_move_10_up'],
        #'avg_move_10_down': sign_diff_dict['avg_move_10_down'],
        'change': str(round(changes_daily[changes_length - 1] * 100, 3)) + '%',
        'record_count': changes_length,
        'self_correlation': sign_diff_dict['self_correlation'],
        'sigma': str(round(stdev * 100, 3)) + '%',
        'sigma_change': round(sigma_change, 3),
        #'sign_diff_pct_10_up':  sign_diff_dict['sign_diff_pct_10_up'],
        #'sign_diff_pct_20_up':  sign_diff_dict['sign_diff_pct_20_up'],
        #'sign_diff_pct_10_down':  sign_diff_dict['sign_diff_pct_10_down'],
        #'sign_diff_pct_20_down':  sign_diff_dict['sign_diff_pct_20_down'],
        #'stdev_10_up': sign_diff_dict['stdev_10_up'],
        #'stdev_10_down': sign_diff_dict['stdev_10_down']
    }
    return sigma_data

cdef process_ticker(ticker, char timestamps[][12]):
    """Makes requests to get crumb and data and call stats computation."""
    url = 'https://finance.yahoo.com/quote/%s/history?p=%s' % (ticker, ticker)
    print('url = %s' % url)

    response = requests.get(url)
    
    cdef char title_c[128]
    memset(title_c, 0, 128)

    cdef char crumb_c[128]
    memset(crumb_c, 0, 128)

    resp_encode = response.text.encode('UTF-8')
    cdef const char* response_text_char = resp_encode
    get_title(response_text_char, title_c)
    get_crumb(response_text_char, crumb_c)

    crumb = crumb_c.decode('UTF-8')
    download_url = ('https://query1.finance.yahoo.com/v7/finance/download/%s?'
                    'period1=%s&period2=%s&interval=1d&events=history'
                    '&crumb=%s' % (ticker, timestamps[1].decode('UTF-8'), timestamps[0].decode('UTF-8'), crumb))
    print('download_url = %s' % download_url)

    title = title_c.decode('UTF-8')

    download_response = requests.get(download_url, cookies=response.cookies)

    # cdef double* adj_close
    cdef double changes_daily[512]

    dl_resp_char = download_response.text.encode('UTF-8')
    cdef char *download_response_char = dl_resp_char

    start = time.time_ns()
    cdef int changes_length = get_adj_close_and_changes(download_response_char, changes_daily)
    end = time.time_ns()
    print('ran get_adj_close_and_changes() in %d ns' % (end - start))

    if not changes_length:
        return None

    sigma_data = get_sigma_data(changes_daily, changes_length)
    sigma_data['c_name'] = title
    sigma_data['c_ticker'] = ticker

    return sigma_data

cdef process_tickers(ticker_list, char timestamps[][12]):
    """Processes all of the input tickers by looping over the list."""
    symbol_count = 0
    for symbol in ticker_list:
        ticker = symbol.strip().upper()
        sigma_data = process_ticker(ticker, timestamps)
        if sigma_data:
            print(ujson.dumps(sigma_data, sort_keys=True, indent=2))

        symbol_count += 1
        if symbol_count < len(sys.argv[1:]):
            time.sleep(1.5)

def main():
    """The main routine and application entry point of this module."""
    cdef char timestamps[2][12]
    get_timestamps(timestamps)

    if len(sys.argv) < 2:
        while True:
            raw_ticker_string = input('Enter ticker list: ')

            start = time.time()
            ticker_list = raw_ticker_string.strip().split(' ')

            process_tickers(ticker_list, timestamps)

            end = time.time()
            print('processed in %.6f' % (end - start))
        return

    ticker_list = [s.strip().upper() for s in sys.argv[1:]]
    process_tickers(ticker_list, timestamps)

if __name__ == '__main__':
    main()
