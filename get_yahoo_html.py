import datetime
from datetime import date
import json
import sys
import time
import numpy
import requests


def get_crumb(response):
    crumbstore_start_idx = response.text.find("CrumbStore")
    json_start = response.text[crumbstore_start_idx + 12:crumbstore_start_idx + 70]
    json_end_idx = json_start.find("},")
    json_snippet = json_start[:json_end_idx + 1]
    json_obj = json.loads(json_snippet)
    return json_obj['crumb']

def get_timestamps():
    manana = date.today() + datetime.timedelta(days=1)
    ago_366_days = manana + datetime.timedelta(days=-367)
    manana_stamp = time.mktime(manana.timetuple())
    ago_366_days_stamp = time.mktime(ago_366_days.timetuple())
    return (manana_stamp, ago_366_days_stamp)

def get_title(response):
    title_start_idx = response.text.find('<title>')
    title_start = response.text[title_start_idx:]
    pipe_start = title_start.find('|') + 2
    hyphen_end = title_start.find('-')
    return title_start[pipe_start:hyphen_end].strip()

def get_adj_close_and_changes(response_text):
    #start = time.time()

    lines = response_text.split('\n')
    data_lines = lines[1:-1]
    len_data_lines = len(data_lines)
    adj_prices = numpy.zeros(len_data_lines)
    changes = numpy.zeros(len_data_lines - 1)
    for i, line in enumerate(data_lines):
        cols = line.split(',')
        if cols[5] == 'null':
            print('===== "null" values found in the input ====')
            print('===== continuing ..... ====================')
            return (None, None)
        adj_close = float(cols[5])
        adj_prices[i] = adj_close
        if i:
            changes[i-1] = (adj_close - adj_prices[i-1])/adj_prices[i-1]
    #end = time.time()
    #print('ran get_adj_close_and_changes() in %d.' % (end - start))

    return (adj_prices, changes)

def compute_sign_diff_pct(ticker_changes):
    changes_0 = ticker_changes[1:-1]
    changes_minus_one = ticker_changes[:-2]

    changes_tuples = zip(changes_minus_one, changes_0)
    sorted_descending = list(reversed(sorted(changes_tuples, key=lambda b: b[0])))

    # UP
    pct_sum_10_up = 0
    pct_sum_20_up = 0
    np_avg_10_up = numpy.zeros(10)
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
    np_avg_10_down = numpy.zeros(10)
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
    sign_diff_dict = compute_sign_diff_pct(changes_daily)

    changes_numpy = changes_daily[:-1]
    stdev = numpy.std(changes_numpy, ddof=1)
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
    url = 'https://finance.yahoo.com/quote/%s/history?p=%s' % (ticker, ticker)
    print('url = %s' % url)

    response = requests.get(url)
    cookie_jar = response.cookies
    crumb = get_crumb(response)

    download_url = ('https://query1.finance.yahoo.com/v7/finance/download/%s?'
                    'period1=%d&period2=%d&interval=1d&events=history'
                    '&crumb=%s' % (ticker, ago_366_days_stamp, manana_stamp, crumb))
    print('download_url = %s' % download_url)
    download_response = requests.get(download_url, cookies=cookie_jar)

    adj_close, changes_daily = get_adj_close_and_changes(download_response.text)
    if adj_close is None:
        return None

    sigma_data = get_sigma_data(changes_daily)
    sigma_data['c_name'] = get_title(response)
    sigma_data['c_ticker'] = ticker
    return sigma_data

def process_tickers(ticker_list):
    (manana_stamp, ago_366_days_stamp) = get_timestamps()
    symbol_count = 0

    for symbol in ticker_list:
        ticker = symbol.strip().upper()
        sigma_data = process_ticker(ticker, manana_stamp, ago_366_days_stamp)
        if sigma_data:
            print(json.dumps(sigma_data, sort_keys=True, indent=2))

        symbol_count += 1
        if symbol_count < len(sys.argv[1:]):
            time.sleep(1.5)

def main():
    if len(sys.argv) < 2:
        while True:
            raw_ticker_string = input('Enter ticker list: ')

            #start = time.time()
            ticker_list = raw_ticker_string.strip().split(' ')

            process_tickers(ticker_list)

            #end = time.time()
            #print('processed in %.6f' % (end - start))
        return

    ticker_list = [s.strip().upper() for s in sys.argv[1:]]
    process_tickers(ticker_list)

if __name__ == '__main__':
    main()
