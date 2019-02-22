#include <stdio.h>
#include <string.h>
#include <uwsgi.h>
#include <curl/curl.h>
#include "compute_statistics.h"

static CURL *curl;
static char timestamps[2][12];

int stats_uwsgi(struct wsgi_request *req) {
    if (curl == NULL) {
        curl = create_and_init_curl();
        get_timestamps(timestamps);
    }

    if (uwsgi_parse_vars(req)) {
        return -1;
    }

    const char *path_info = req->path_info;
    const char *form_feed = strstr(path_info, "\f");
    char path_buf[128];
    memset(path_buf, 0, 128);
    strncpy(path_buf, path_info, strlen(path_info) - strlen(form_feed));

    const char *path_prefix = "/compute_statistics/";
    if (strncmp(path_prefix, path_buf, strlen(path_prefix)) == 0) {
        const char * comp_stats_path = &path_buf[20];

        if (strlen(comp_stats_path) > 0) {
            sign_diff_pct sign_diff_values;
            run_stats(comp_stats_path, &sign_diff_values, curl, timestamps);

            char sign_diff_json[512];
            build_sign_diff_print_json(sign_diff_json, &sign_diff_values);

            uwsgi_response_prepare_headers_int(req, 200);
            uwsgi_response_add_content_type(req, "application/json", 16);
            uwsgi_response_write_body_do(req, sign_diff_json, strlen(sign_diff_json));
        } else {
            const char *failure_json = "{\"status\":\"failed\"}";
            uwsgi_response_prepare_headers_int(req, 200);
            uwsgi_response_add_content_type(req, "application/json", 16);
            uwsgi_response_write_body_do(req, failure_json, strlen(failure_json));
        }
    } else {
        uwsgi_response_prepare_headers_int(req, 404);
        uwsgi_response_write_body_do(req, "404 Not Found", 13);
    }

    return UWSGI_OK;
}