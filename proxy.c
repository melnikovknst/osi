#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>

#include "proxy.h"
#include "config.h"
#include "net.h"
#include "http.h"
#include "logger.h"

typedef struct {
    proxy_ctx_t *px;
    int client_fd;
    struct sockaddr_in addr;
} client_job_t;

int safe_close(int fd) {
    if (fd >= 0) 
        return close(fd);
    return 0;
}

static void close_client_job(client_job_t *cj) {
    if (!cj) 
        return;
    safe_close(cj->client_fd);
    free(cj);
}

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

static int stream_reader_to_client(record_t *r, int fd) {
    size_t off = 0;
    while (1) {
        const void *ptr;
        size_t len;
        int done = 0;
        int canceled = 0;
        len = rec_wait_chunk(r, &off, &ptr, &len, &done, &canceled);
        if (canceled) 
            return -1;
        if (len > 0) {
            if (send_all(fd, ptr, len)) 
                return -1;
            off += len;
            continue;
        }
        if (done) {
            if (off >= rec_size(r)) 
                return 0;
            continue;
        }
    }
}

static int fetch_and_stream(proxy_ctx_t *px, record_t *r, const http_request_t *req, int client_fd) {
    int us = net_connect_host(req->host, req->port, CONNECT_TIMEOUT_MS);
    if (us < 0) {
        log_err("connect upstream %s:%d failed", req->host, req->port);
        rec_cancel(&px->cache, r);
        return -1;
    }
    set_timeouts(us, IDLE_RW_MS, IDLE_RW_MS);

    char reqbuf[4096];
    int qlen = http_build_upstream_get(reqbuf, sizeof reqbuf, req);
    if (send_all(us, reqbuf, (size_t) qlen)) {
        log_err("send upstream failed");
        safe_close(us);
        rec_cancel(&px->cache, r);
        return -1;
    }

    int initiator_alive = (client_fd >= 0);
    char buf[64 * 1024];

    while (1) {
        ssize_t n = recv(us, buf, sizeof buf, 0);
        if (n == 0) 
            break;
        if (n < 0) {
            if (errno == EINTR) 
                continue;
            log_err("recv upstream err: %s", strerror(errno));
            safe_close(us);
            rec_cancel(&px->cache, r);
            return -1;
        }

        if (initiator_alive) {
            if (send_all(client_fd, buf, (size_t) n) != 0) 
                initiator_alive = 0;
        }

        if (rec_append(&px->cache, r, buf, (size_t) n)) {
            log_err("append failed");
            safe_close(us);
            rec_cancel(&px->cache, r);
            return -1;
        }
    }

    safe_close(us);
    rec_finish(&px->cache, r);
    return 0;
}

static void handle_client(void *arg) {
    client_job_t *cj = (client_job_t *) arg;
    proxy_ctx_t *px = cj->px;
    int fd = cj->client_fd;
    set_timeouts(fd, IDLE_RW_MS, IDLE_RW_MS);

    http_request_t req;
    if (http_parse_client_request(fd, &req) != 0) {
        const char *resp = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n";
        send_all(fd, resp, strlen(resp));
        close_client_job(cj);
        return;
    }

    char key[4096];
    snprintf(
        key,
        sizeof key,
        "http://%s:%d%s",
        req.host,
        req.port,
        req.path[0] ? req.path : "/"
    );

    cache_acquire_t acq = (cache_acquire_t) {0};
    cache_acquire(&px->cache, key, &acq);

    if (acq.is_fetcher) {
        log_info("MISS+FETCH %s", key);
        (void) fetch_and_stream(px, acq.rec, &req, fd);
    } else {
        if (rec_is_completed(acq.rec)) {
            log_info("HIT %s", key);
            rec_touch_lru(&px->cache, acq.rec);
        } else {
            log_info("JOIN %s", key);
        }
        (void) stream_reader_to_client(acq.rec, fd);
    }

    cache_release(acq.rec);
    close_client_job(cj);
}

int proxy_init(proxy_ctx_t *px, int port, int workers) {
    px->listen_fd = net_listen(port);
    if (px->listen_fd < 0) 
        return -1;
    if (cache_init(&px->cache, N_BUCKETS, SOFT_LIMIT_BYTES)) 
        return -1;
    px->workers = workers;
    if (tp_init(&px->tp, px->workers, QUEUE_CAP)) 
        return -1;
    return 0;
}

void proxy_run_accept_loop(proxy_ctx_t *px) {
    log_info("listening on port %d, workers=%d, buckets=%d", PROXY_PORT, px->workers, (int) N_BUCKETS);
    while (1) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof sa;
        int cfd = accept(px->listen_fd, (struct sockaddr *) &sa, &sl);
        if (cfd < 0) {
            if (errno == EINTR) 
                continue;
            log_err("accept: %s", strerror(errno));
            break;
        }
        client_job_t *cj = calloc(1, sizeof *cj);
        if (!cj) {
            safe_close(cfd);
            continue;
        }
        cj->px = px;
        cj->client_fd = cfd;
        cj->addr = sa;
        tp_submit(&px->tp, handle_client, cj);
    }
}

void proxy_shutdown(proxy_ctx_t *px) {
    tp_poison_and_join(&px->tp);
    tp_destroy(&px->tp);
    cache_destroy(&px->cache);
    safe_close(px->listen_fd);
}
