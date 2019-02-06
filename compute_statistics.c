#define _DEFAULT_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>

typedef struct {
	char *memory;
	size_t size;
} Memory;

typedef struct {
	char title[128];
} sign_diff_pct;

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

int get_crumb(const char *response_text, char *crumb) {
	const char *crumbstore = strstr(response_text, "CrumbStore");
    const char *colon_quote = strstr(crumbstore, ":\"");
    const char *end_quote = strstr(&colon_quote[2], "\"");
    strncpy(crumb, &colon_quote[2], strlen(&colon_quote[2]) - strlen(end_quote));
    char crumbclean[128];
    memset(crumbclean, 0, 128);
    const char *twofpos = strstr(crumb, "\\u002F");

    if (twofpos) {
        strncpy(crumbclean, crumb, twofpos - crumb);
        strcat(crumbclean, "%2F");
        strcat(crumbclean, &twofpos[6]);
        memset(crumb, 0, 128);
        strcpy(crumb, crumbclean);
    }
	return 0;
}

int get_title(const char *response_text, char *title) {
	const char* title_start = strstr(response_text, "<title>");
    const char* pipe_start = strstr(title_start, "|");
    const char* hyphen_end = strstr(&pipe_start[2], "-");
    size_t diff = strlen(&pipe_start[2]) - strlen(hyphen_end);
    if (diff < 128) {
    	strncpy(title, &pipe_start[2], diff);
    	return 0;
    }
    printf("Failed to parse the title from the response.\n");
	return 1;
}

void process_ticker(char *ticker, char timestamps[][12], CURL *curl) {
    struct timespec start;
    struct timespec end;

    char url[128];
    memset(url, 0, 128);
    sprintf(url, "https://finance.yahoo.com/quote/%s/history?p=%s", ticker, ticker);
	// printf("url = %s\n", url);

    CURLcode response;
    Memory memoria;
    memoria.memory = (char*)malloc(1);
    memoria.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&memoria);

    response = curl_easy_perform(curl);

    if (response != CURLE_OK) {
    	printf("curl_easy_perform() failed.....\n");
    }

    char crumb[128];
    memset(crumb, 0, 128);

    sign_diff_pct sign_diff_values;
    memset(sign_diff_values.title, 0, 128);

    int crumb_failure = get_crumb(memoria.memory, crumb);
    if (crumb_failure) {
    	printf("Failed to get crumb...\n");
    	return;
    }

    int title_failure = get_title(memoria.memory, sign_diff_values.title);
    if (title_failure) {
    	return;
    }

    char download_url[256];
    memset(download_url, 0, 256);
    sprintf(download_url, "https://query1.finance.yahoo.com/v7/finance/download/%s?period1=%s&period2=%s&interval=1d&events=history&crumb=%s", ticker, timestamps[1], timestamps[0], crumb);
    printf("download_url = %s\n", download_url);
}

void process_tickers(char *ticker_string, char timestamps[][12], CURL *curl) {
    char *ticker = strsep(&ticker_string, " ");
    while (ticker != NULL) {
    	process_ticker(ticker, timestamps, curl);
    	ticker = strsep(&ticker_string, " ");
    	if (ticker != NULL) {
    		usleep(1500000);
    	}
    }
}

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t rs = size * nmemb;
    Memory *mem = (Memory*)userp;
    char *ptr = (char*)realloc(mem->memory, mem->size + rs + 1);
    if (ptr == NULL) {
        printf("Insufficient memory: realloc() returned NULL.\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, rs);
    mem->size += rs;
    mem->memory[mem->size] = 0;
    return rs;
}

int main(void) {
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_callback);

    char timestamps[2][12];
    get_timestamps(timestamps);
    struct timespec start;
    struct timespec end;

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

        clock_gettime(CLOCK_MONOTONIC, &start);
        process_tickers(ticker_string_strip, timestamps, curl);
        clock_gettime(CLOCK_MONOTONIC, &end);
        printf("processed in %.5f s\n", ((double)end.tv_sec + 1.0e-9*end.tv_nsec) - ((double)start.tv_sec + 1.0e-9*start.tv_nsec));
    }
    curl_easy_cleanup(curl);
    
    return EXIT_SUCCESS;
}
