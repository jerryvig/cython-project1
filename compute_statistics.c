#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>

void get_timestamps(char timestamps[][12]) {
	memset(timestamps[0], 0, 12);
    memset(timestamps[1], 0, 12);
    time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    now_tm->tm_sec = 0;
    now_tm->tm_min = 0;
    now_tm->tm_hour = 0;
    time_t today_time = mktime(now_tm);
    time_t manana = today_time + 86400;
    time_t ago_366_days = today_time - 31622400;
    sprintf(timestamps[0], "%ld", manana);
    sprintf(timestamps[1], "%ld", ago_366_days);
}

void process_tickers(char *ticker_string, char timestamps[][12], CURL *curl) {
	printf("ticker_string = %s\n", ticker_string);
}

int main(void) {
	CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    // curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);

    char timestamps[2][12];
    get_timestamps(timestamps);

	int ticker_strlen;
    char ticker_string[128];
    char ticker_string_strip[128];

    while (1) {
    	memset(ticker_string, 0, 128);
    	memset(ticker_string_strip, 0, 128);
    	printf("%s", "Enter ticker list: ");
    	fflush(stdout);
        fgets(ticker_string, 128, stdin);
        ticker_strlen = strlen(ticker_string) - 1;
        strncpy(ticker_string_strip, ticker_string, ticker_strlen);
        for (int i=0; i<ticker_strlen; ++i) {
        	ticker_string_strip[i] = toupper(ticker_string_strip[i]);
        }
        
        process_tickers(ticker_string_strip, timestamps, curl);
    }
    curl_easy_cleanup(curl);
	return EXIT_SUCCESS;
}