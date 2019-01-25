from concurrent.futures import ThreadPoolExecutor
import datetime
from datetime import date
import ujson
import sys
import time
import numpy
import requests

from libc.stdio cimport printf
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


cdef void get_crumb(const char *response_text, char *crumb):
    cdef const char *crumbstore = strstr(response_text, "CrumbStore")
    cdef const char *colon_quote = strstr(crumbstore, ":\"")
    cdef const char *end_quote = strstr(&colon_quote[2], "\"")
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) - strlen(end_quote))
    return

def get_timestamps():
    """Computes the start and end timestamps for the request."""
    manana = date.today() + datetime.timedelta(days=1)
    ago_366_days = manana + datetime.timedelta(days=-367)
    manana_stamp = time.mktime(manana.timetuple())
    ago_366_days_stamp = time.mktime(ago_366_days.timetuple())
    return (manana_stamp, ago_366_days_stamp)

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
    return 1

def compute_sign_diff_pct(ticker_changes):
    """Computes sign-diffs for up and down 10 and 20 blocks."""
    changes_0 = ticker_changes[1:-1]
    changes_minus_one = ticker_changes[:-2]

    # You are trying to fix this up.
    changes_tuples = numpy.column_stack([changes_minus_one, changes_0])
    sorted_descending = changes_tuples[changes_tuples[:, 0].argsort()[::-1]]

    # UP
    pct_sum_10_up = 0
    pct_sum_20_up = 0
    np_avg_10_up = numpy.zeros(10, dtype=float)
    for i, ele in enumerate(sorted_descending[:20]):
        product = ele[0] * ele[1]
        if i < 10:
            np_avg_10_up[i] = ele[1]
        if product:
            is_diff = -0.5*numpy.sign(product) + 0.5
            if i < 10:
                pct_sum_10_up += is_diff
            pct_sum_20_up += is_diff
    avg_10_up = numpy.average(np_avg_10_up)
    stdev_10_up = numpy.std(np_avg_10_up, ddof=1)

    # DOWN
    pct_sum_10_down = 0
    pct_sum_20_down = 0
    np_avg_10_down = numpy.zeros(10, dtype=float)
    for i, ele in enumerate(sorted_descending[-20:]):
        product = ele[0] * ele[1]
        if i > 9:
            np_avg_10_down[i-10] = ele[1]
        if product:
            is_diff = -0.5*numpy.sign(product) + 0.5
            if i > 9:
                pct_sum_10_down += is_diff
            pct_sum_20_down += is_diff
    avg_10_down = numpy.average(np_avg_10_down)
    stdev_10_down = numpy.std(np_avg_10_down, ddof=1)

    self_correlation = numpy.corrcoef([changes_minus_one, changes_0])[1, 0]

    return {
        'avg_move_10_up': str(round(avg_10_up * 100, 4)) + '%',
        'avg_move_10_down': str(round(avg_10_down * 100, 4)) + '%',
        'self_correlation': str(round(self_correlation * 100, 3)) + '%',
        'sign_diff_pct_10_up':  str(round(pct_sum_10_up * 10, 4)) + '%',
        'sign_diff_pct_20_up':  str(round(pct_sum_20_up * 5, 4)) + '%',
        'sign_diff_pct_10_down': str(round(pct_sum_10_down * 10, 4)) + '%',
        'sign_diff_pct_20_down': str(round(pct_sum_20_down * 5, 4)) + '%',
        'stdev_10_up': str(round(stdev_10_up * 100, 4)) + '%',
        'stdev_10_down': str(round(stdev_10_down * 100, 4)) + '%'
    }

def get_sigma_data(changes_daily):
    """Computes standard change/standard deviation and constructs dict object."""
    sign_diff_dict = compute_sign_diff_pct(changes_daily)

    stdev = numpy.std(changes_daily[:-1], ddof=1)
    sigma_change = changes_daily[-1]/stdev

    sigma_data = {
        'avg_move_10_up': sign_diff_dict['avg_move_10_up'],
        'avg_move_10_down': sign_diff_dict['avg_move_10_down'],
        'change': str(round(changes_daily[-1] * 100, 3)) + '%',
        'record_count': len(changes_daily),
        'self_correlation': sign_diff_dict['self_correlation'],
        'sigma': str(round(stdev * 100, 3)) + '%',
        'sigma_change': round(sigma_change, 3),
        'sign_diff_pct_10_up':  sign_diff_dict['sign_diff_pct_10_up'],
        'sign_diff_pct_20_up':  sign_diff_dict['sign_diff_pct_20_up'],
        'sign_diff_pct_10_down':  sign_diff_dict['sign_diff_pct_10_down'],
        'sign_diff_pct_20_down':  sign_diff_dict['sign_diff_pct_20_down'],
        'stdev_10_up': sign_diff_dict['stdev_10_up'],
        'stdev_10_down': sign_diff_dict['stdev_10_down']
    }
    return sigma_data

def process_ticker(ticker, manana_stamp, ago_366_days_stamp):
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
                    'period1=%d&period2=%d&interval=1d&events=history'
                    '&crumb=%s' % (ticker, ago_366_days_stamp, manana_stamp, crumb))
    print('download_url = %s' % download_url)

    title = title_c.decode('UTF-8')
    print('title = "%s"' % title)

    with ThreadPoolExecutor(max_workers=2) as executor:
        request_future = executor.submit(
            requests.get, download_url, cookies=response.cookies)
        # title_future = executor.submit(get_title, response)
        # title = title_future.result()
        download_response = request_future.result()

    # cdef double* adj_close
    cdef double changes_daily[512]

    dl_resp_char = download_response.text.encode('UTF-8')
    cdef char *download_response_char = dl_resp_char

    #adj_close, changes_daily = get_adj_close_and_changes(download_response.text)
    start = time.time_ns()
    cdef int get_adj_close_success = get_adj_close_and_changes(download_response_char, changes_daily)

    end = time.time_ns()
    print('ran get_adj_close_and_changes() in %d ns' % (end - start))

    

    if not get_adj_close_success:
        return None

    # free(changes_daily)

    exit(0)

    changes_daily_ii = []
    sigma_data = get_sigma_data(changes_daily_ii)
    sigma_data['c_name'] = title
    sigma_data['c_ticker'] = ticker

    return sigma_data

def process_tickers(ticker_list):
    """Processes all of the input tickers by looping over the list."""
    (manana_stamp, ago_366_days_stamp) = get_timestamps()
    symbol_count = 0

    for symbol in ticker_list:
        ticker = symbol.strip().upper()
        sigma_data = process_ticker(ticker, manana_stamp, ago_366_days_stamp)
        if sigma_data:
            print(ujson.dumps(sigma_data, sort_keys=True, indent=2))

        symbol_count += 1
        if symbol_count < len(sys.argv[1:]):
            time.sleep(1.5)

def main():
    """The main routine and application entry point of this module."""
    if len(sys.argv) < 2:
        while True:
            raw_ticker_string = input('Enter ticker list: ')

            start = time.time()
            ticker_list = raw_ticker_string.strip().split(' ')

            process_tickers(ticker_list)

            end = time.time()
            print('processed in %.6f' % (end - start))
        return

    ticker_list = [s.strip().upper() for s in sys.argv[1:]]
    process_tickers(ticker_list)

if __name__ == '__main__':
    main()
