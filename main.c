#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "config.h"
#include "threadpool.h"
#include "cache.h"
#include "net.h"
#include "http.h"

#define TEST_PORT 18080
#define FIRST_BYTE_MS 10000
#define IDLE_RW_MS 30000

static int send_all_local(int fd, const void *buf, size_t n) {
    const char *p = (const char *) buf;
    size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
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

static void *server_thread(void *arg) {
    (void) arg;
    int lfd = net_listen(TEST_PORT);
    if (lfd < 0) {
        perror("net_listen");
        return NULL;
    }

    for (int i = 0; i < 2; i++) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof sa;
        int cfd = accept(lfd, (struct sockaddr *) &sa, &sl);
        if (cfd < 0) {
            if (errno == EINTR) {
                i--;
                continue;
            }
            perror("accept");
            break;
        }

        set_timeouts(cfd, FIRST_BYTE_MS, IDLE_RW_MS);

        http_request_t req;
        int rc = http_parse_client_request(cfd, &req);

        if (rc != 0) {
            char msg[128];
            int n = snprintf(msg, sizeof msg, "ERR %d\n", rc);
            send_all_local(cfd, msg, (size_t) n);
            close(cfd);
            continue;
        }

        char out[4096];
        int n = http_build_upstream_get(out, sizeof out, &req);

        char head[1024];
        int m = snprintf(
            head,
            sizeof head,
            "OK\nmethod=%s\nurl=%s\nversion=%s\nhost=%s\nport=%d\npath=%s\n---UPSTREAM---\n",
            req.method,
            req.url,
            req.version,
            req.host,
            req.port,
            req.path
        );
        send_all_local(cfd, head, (size_t) m);
        if (n > 0) 
            send_all_local(cfd, out, (size_t) n);
        close(cfd);
    }

    close(lfd);
    return NULL;
}

static void run_client(const char *name, const char *raw_req) {
    int fd = net_connect_host("127.0.0.1", TEST_PORT, 3000);
    if (fd < 0) {
        fprintf(stderr, "[%s] connect failed\n", name);
        return;
    }
    set_timeouts(fd, 3000, 3000);

    if (send_all_local(fd, raw_req, strlen(raw_req)) != 0) {
        fprintf(stderr, "[%s] send failed\n", name);
        close(fd);
        return;
    }

    char buf[8192];
    ssize_t n;
    printf("=== %s: server reply ===\n", name);
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        fwrite(buf, 1, (size_t) n, stdout);
    }
    printf("\n=== %s: end ===\n\n", name);
    close(fd);
}

int main(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, server_thread, NULL) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 1;
    }
    usleep(100 * 1000);

    const char *req_abs =
        "GET http://example.com/ HTTP/1.1\r\n"
        "User-Agent: test\r\n"
        "Accept: */*\r\n"
        "\r\n";

    const char *req_path_host =
        "GET /hello/world HTTP/1.1\r\n"
        "Host: neverssl.com:80\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    run_client("ABSOLUTE", req_abs);
    run_client("PATH+HOST", req_path_host);

    pthread_join(th, NULL);
    return 0;
}
