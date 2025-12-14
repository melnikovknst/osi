#pragma once
#include <stddef.h>
#include <stdint.h>

#define N_BUCKETS 4096
#define BLOCK_SZ (64*1024)
#define SOFT_LIMIT_BYTES (1024ULL<<20) 

#define WORKERS 4
#define QUEUE_CAP 1000
#define PROXY_PORT 8080
#define LISTEN_BACKLOG 512

#define CONNECT_TIMEOUT_MS 5000
#define IDLE_RW_MS 30000
#define FIRST_BYTE_MS 10000