#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>

#include "http.h"
#include "config.h"

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0) 
            return 0;
        if (n < 0) {
            if (errno == EINTR) 
                continue;
                
            return -1;
        }

        buf[i++] = c;
        if (i >= 2 && buf[i - 2] == '\r' && buf[i - 1] == '\n') {
            buf[i] = 0;
            return (ssize_t) i;
        }
    }
    buf[cap - 1] = 0;
    return (ssize_t) i;
}

static int parse_url(const char *url, char *host, int *port, char *path) {
    if (strncmp(url, "http://", 7) == 0) {
        const char *p = url + 7;
        const char *slash = strchr(p, '/');
        const char *h_end = slash ? slash : (url + strlen(url));
        const char *colon = memchr(p, ':', h_end - p);
        if (colon) {
            memcpy(host, p, colon - p);
            host[colon - p] = 0;
            *port = atoi(colon + 1);
        } else {
            memcpy(host, p, h_end - p);
            host[h_end - p] = 0;
            *port = 80;
        }

        if (slash) 
            strcpy(path, slash);
        else 
            strcpy(path, "/");
        return 0;
    } else if (url[0] == '/') {
        strcpy(path, url);
        return 1;
    }
    return -1;
}

int http_parse_client_request(int fd, http_request_t *req) {
    memset(req, 0, sizeof *req);
    char line[4096];
    ssize_t n = read_line(fd, line, sizeof line);
    if (n <= 0) 
        return -1;
    char u[2048] = {0};
    char m[16] = {0};
    char v[16] = {0};
    if (sscanf(line, "%7s %2047s %15s", m, u, v) != 3) 
        return -2;

    strcpy(req->method, m);
    strcpy(req->url, u);
    strcpy(req->version, v);

    if (strcmp(req->method, "GET") != 0) 
        return -3;

    char host_from_hdr[1024] = {0};
    while (1) {
        n = read_line(fd, line, sizeof line);
        if (n < 0) 
            return -4;
        if (n == 0) 
            return -4;
        if (strcmp(line, "\r\n") == 0) 
            break;
        if (strncasecmp(line, "Host:", 5) == 0) {
            const char *p = line + 5;
            while (*p == ' ' || *p == '\t') 
                ++p;
            size_t L = strcspn(p, "\r\n");
            if (L > 0) {
                if (L >= sizeof(host_from_hdr)) 
                    L = sizeof(host_from_hdr) - 1;
                memcpy(host_from_hdr, p, L);
                host_from_hdr[L] = 0;
            }
        }
    }

    int r = parse_url(req->url, req->host, &req->port, req->path);
    if (r == 0) {
    } else if (r == 1) {
        if (!*host_from_hdr) 
            return -5;
        char *colon = strchr(host_from_hdr, ':');
        if (colon) {
            *colon = 0;
            strcpy(req->host, host_from_hdr);
            req->port = atoi(colon + 1);
        } else {
            strcpy(req->host, host_from_hdr);
            req->port = 80;
        }
    } else {
        return -6;
    }

    return 0;
}

int http_build_upstream_get(char *out, size_t cap, const http_request_t *req) {
    return snprintf(
        out,
        cap,
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: Proxy/1.0\r\n"
        "\r\n",
        req->path[0] ? req->path : "/",
        req->host[0] ? req->host : ""
    );
}
