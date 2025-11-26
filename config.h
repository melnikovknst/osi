#pragma once
#include <stddef.h>
#include <stdint.h>

#define N_BUCKETS 4096
#define BLOCK_SZ (64*1024)
#define SOFT_LIMIT_BYTES (128ULL<<20) 
