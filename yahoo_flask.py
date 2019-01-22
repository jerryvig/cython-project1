from flask import Flask
from flask import request
import get_yahoo_html

APP = Flask(__name__)

@APP.route('/test')
def test():
    print('tickers = %s' % request.args.get('tickers'))
    ticker_list = request.args.get('tickers').split(' ')
    get_yahoo_html.process_tickers(ticker_list)
    return '===== DONE ===== '
