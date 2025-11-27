#include "proxy.h"
#include "net.h"
#include "http.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

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

int proxy_init(proxy_ctx_t *px, int port) {
    px->port = port;
    px->listen_fd = net_listen(port);
    if (px->listen_fd < 0)
        return -1;
    
    return 0;
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

        const char *resp = "HTTP/1.0 501 Not Implemented\r\nConnection: close\r\n\r\n";
        send_all(cfd, resp, strlen(resp));
        close(cfd);
    }
}

void proxy_shutdown(proxy_ctx_t *px) {
    if (px->listen_fd >= 0) 
        close(px->listen_fd);
}
