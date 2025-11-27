#pragma once
typedef struct {
    int listen_fd;
    int port;
} proxy_ctx_t;

int  proxy_init(proxy_ctx_t *px, int port);
void proxy_run_accept_loop(proxy_ctx_t *px);
void proxy_shutdown(proxy_ctx_t *px);
