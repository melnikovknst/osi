#include "proxy.h"
#include "net.h"
#include "http.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

#define CONNECT_TIMEOUT_MS 5000
#define IDLE_RW_MS 30000
#define FIRST_BYTE_MS 10000
#define QUEUE_CAP 1000

typedef struct {
    proxy_ctx_t *px;
    int client_fd;
} client_job_t;

static int send_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *) buf;
    size_t left = n;
    while (left) {
        ssize_t w = send(fd, p, left, 0);
        if (w < 0) {
            if (errno == EINTR) 
                continue;
            return -1;
        }
        if (w == 0) 
            return -1;
        left -= (size_t) w;
        p += w;
    }
    return 0;
}

int proxy_init(proxy_ctx_t *px, int port, int workers) {
    px->port = port;
    px->listen_fd = net_listen(port);
    if (px->listen_fd < 0) return -1;

    px->workers = workers;
    if (tp_init(&px->tp, px->workers, QUEUE_CAP) != 0) {
        close(px->listen_fd);
        return -1;
    }
    return 0;
}

static int pass_through_to_upstream(const http_request_t *req, int client_fd) {
    int us = net_connect_host(req->host, req->port, CONNECT_TIMEOUT_MS);
    if (us < 0) {
        const char *resp = "HTTP/1.0 502 Bad Gateway\r\nConnection: close\r\n\r\n";
        send_all(client_fd, resp, strlen(resp));
        return -1;
    }

    set_timeouts(us, FIRST_BYTE_MS, IDLE_RW_MS);

    char reqbuf[4096];
    int qlen = http_build_upstream_get(reqbuf, sizeof reqbuf, req);
    if (send_all(us, reqbuf, (size_t) qlen) != 0) {
        close(us);
        const char *resp = "HTTP/1.0 502 Bad Gateway\r\nConnection: close\r\n\r\n";
        send_all(client_fd, resp, strlen(resp));
        return -1;
    }

    char buf[64 * 1024];
    int got_any = 0;

    while (1) {
        ssize_t n = recv(us, buf, sizeof buf, 0);
        if (n == 0) 
            break;
        if (n < 0) {
            if (errno == EINTR) 
                continue;

            int e = errno;
            close(us);

            if (!got_any && (e == EAGAIN || e == EWOULDBLOCK)) {
                const char *resp = "HTTP/1.0 504 Gateway Timeout\r\nConnection: close\r\n\r\n";
                send_all(client_fd, resp, strlen(resp));
            } else {
                const char *resp = "HTTP/1.0 502 Bad Gateway\r\nConnection: close\r\n\r\n";
                if (!got_any) 
                    send_all(client_fd, resp, strlen(resp));
            }
            return -1;
        }

        if (!got_any) {
            got_any = 1;
            set_timeouts(us, IDLE_RW_MS, IDLE_RW_MS);
        }

        if (send_all(client_fd, buf, (size_t) n) != 0) 
            break;
    }

    close(us);
    return 0;
}


static void handle_client(void *arg) {
    client_job_t *cj = (client_job_t*)arg;
    int cfd = cj->client_fd;

    set_timeouts(cfd, FIRST_BYTE_MS, IDLE_RW_MS);

    http_request_t req;
    int rc = http_parse_client_request(cfd, &req);
    if (rc != 0) {
        const char *resp = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n";
        send_all(cfd, resp, strlen(resp));
        close(cfd);
        free(cj);
        return;
    }

    (void)pass_through_to_upstream(&req, cfd);

    close(cfd);
    free(cj);
}


void proxy_run_accept_loop(proxy_ctx_t *px) {
    printf("listening on port %d", px->port);
    while (1) {
        int cfd = accept(px->listen_fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) 
                continue;

            printf("accept: %s", strerror(errno));
            break;
        }

        http_request_t req;
        int rc = http_parse_client_request(cfd, &req);
        if (rc != 0) {
            const char *resp = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n";
            send_all(cfd, resp, strlen(resp));
            close(cfd);
            continue;
        }

        client_job_t *cj = (client_job_t*)malloc(sizeof *cj);
        if (!cj) { close(cfd); continue; }
        cj->px = px;
        cj->client_fd = cfd;

        if (tp_submit(&px->tp, handle_client, cj) != 0) {
            close(cfd);
            free(cj);
        }
    }
}

void proxy_shutdown(proxy_ctx_t *px) {
    tp_poison_and_join(&px->tp);
    tp_destroy(&px->tp);
    if (px->listen_fd >= 0) close(px->listen_fd);
}

