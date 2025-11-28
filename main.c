#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "proxy.h"
#include "config.h"
#include "logger.h"

static proxy_ctx_t gpx;
static volatile sig_atomic_t stop_flag = 0;
static void on_sigint(int s) {
    (void)s;
    stop_flag = 1;
    close(gpx.listen_fd);
}

int main() {
    if (proxy_init(&gpx, PROXY_PORT, WORKERS)) {
        log_err("init failed");
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    proxy_run_accept_loop(&gpx);
    proxy_shutdown(&gpx);

    log_info("finishing");
    return 0;
}
