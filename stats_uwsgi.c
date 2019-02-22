#include <stdio.h>
#include <string.h>
#include <uwsgi.h>

int stats_uwsgi(struct wsgi_request *req) {
    if (uwsgi_parse_vars(req)) {
        return -1;
    }

    char *path_info = req->path_info;
    char *form_feed = strstr(path_info, "\f");
    char path_buf[128];
    memset(path_buf, 0, 128);
    strncpy(path_buf, path_info, strlen(path_info) - strlen(form_feed));

    char *path_prefix = "/compute_statistics";
    if (strncmp(path_prefix, path_buf, strlen(path_prefix)) == 0) {
        uwsgi_response_prepare_headers_int(req, 200);
        uwsgi_response_add_content_type(req, "application/json", 16);



        uwsgi_response_write_body_do(req, path_buf, strlen(path_buf));
    } else {
    	uwsgi_response_prepare_headers_int(req, 404);
    	uwsgi_response_write_body_do(req, "404 Not Found", 13);
    }

    return UWSGI_OK;
}