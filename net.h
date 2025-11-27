#pragma once
int net_listen(int port);
int net_connect_host(const char *host, int port, int connect_timeout_ms);
int set_timeouts(int fd, int rcv_ms, int snd_ms);