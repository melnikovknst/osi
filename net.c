#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "net.h"
#include "config.h"

#define LISTEN_BACKLOG 512

int set_timeouts(int fd, int rcv_ms, int snd_ms) {
    struct timeval tv;
    if (rcv_ms >= 0) {
        tv.tv_sec = rcv_ms / 1000;
        tv.tv_usec = (rcv_ms % 1000) * 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv)) 
            return -1;
    }
    if (snd_ms >= 0) {
        tv.tv_sec = snd_ms / 1000;
        tv.tv_usec = (snd_ms % 1000) * 1000;
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv)) 
            return -1;
    }
    return 0;
}

int net_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("socket: %s", strerror(errno));
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *) &a, sizeof a)) {
        printf("bind: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, LISTEN_BACKLOG)) {
        printf("listen: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

int net_connect_host(const char *host, int port, int connect_timeout_ms) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    struct addrinfo *rp = NULL;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) 
        return -1;

    int fd = -1;

    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) 
            continue;
        set_timeouts(fd, connect_timeout_ms, connect_timeout_ms);
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) 
            break;
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}
