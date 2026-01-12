#pragma once
#include <stddef.h>

typedef struct {
    char method[8];
    char url[2048];
    char version[16];
    char host[1024];
    int  port;
    char path[2048];
} http_request_t;

int http_parse_client_request(int fd, http_request_t *req);
int http_build_upstream_get(char *out, size_t cap, const http_request_t *req);
