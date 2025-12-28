#pragma once
#include "cache.h"
#include "threadpool.h"

typedef struct {
    int listen_fd;
    cache_t cache;
    threadpool_t tp;
    int workers;
} proxy_ctx_t;

int proxy_init(proxy_ctx_t *px, int port, int workers);
void proxy_run_accept_loop(proxy_ctx_t *px);
void proxy_shutdown(proxy_ctx_t *px);
