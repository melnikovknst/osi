#pragma once
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct record record_t;

typedef struct cache {
    struct bucket {
        pthread_mutex_t m;
        struct entry *head;
    } *b;
    size_t nbuckets;

    pthread_mutex_t lru_m;
    record_t *lru_head, *lru_tail;
    size_t bytes_completed; 
    size_t soft_limit;

    volatile size_t hits, misses, stores, evicts;
} cache_t;

int cache_init(cache_t *c, size_t nbuckets, size_t soft);
void cache_destroy(cache_t *c);

typedef struct {
    record_t *rec;
    int is_fetcher;
} cache_acquire_t;

int cache_acquire(cache_t *c, const char *key, cache_acquire_t *out);

void cache_release(record_t *r);

int rec_append(cache_t *c, record_t *r, const void *buf, size_t n);

void rec_finish(cache_t *c, record_t *r);
void rec_cancel(cache_t *c, record_t *r);

size_t rec_wait_chunk(record_t *r, size_t *off, const void **ptr, size_t *len, int *done, int *canceled);

void rec_touch_lru(cache_t *c, record_t *r);

const char* rec_key(record_t *r);
size_t rec_size(record_t *r);
int rec_is_completed(record_t *r);
