#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "config.h"
#include "threadpool.h"
#include "cache.h"

typedef struct {
    cache_t *c;
    const char *key;
    int chunks;
    int delay_ms;
} writer_arg_t;

typedef struct {
    cache_t *c;
    const char *key;
    const char *name;
} reader_arg_t;

static void writer_job(void *arg) {
    writer_arg_t *a = (writer_arg_t*)arg;
    cache_acquire_t acq = {0};
    cache_acquire(a->c, a->key, &acq);

    if (!acq.is_fetcher) {
        printf("[WRITER %s] someone else is fetcher, exit\n", a->key);
        cache_release(acq.rec);
        free(a);
        return;
    }

    const char *hdr = "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n";
    rec_append(a->c, acq.rec, hdr, strlen(hdr));

    char buf[256];
    for (int i = 0; i < a->chunks; i++) {
        int n = snprintf(buf, sizeof buf, "[%s] chunk-%d\n", a->key, i);
        rec_append(a->c, acq.rec, buf, (size_t)n);
        usleep(a->delay_ms * 1000);
    }

    rec_finish(a->c, acq.rec);
    cache_release(acq.rec);
    free(a);
}

static void reader_job(void *arg) {
    reader_arg_t *a = (reader_arg_t*)arg;
    cache_acquire_t acq = {0};
    cache_acquire(a->c, a->key, &acq);

    size_t off = 0, total = 0;
    while (1) {
        const void *ptr; size_t len; int done = 0, canceled = 0;
        rec_wait_chunk(acq.rec, &off, &ptr, &len, &done, &canceled);
        if (len > 0) {
            total += len;
            off   += len;
            continue;
        }
        if (canceled) {
            printf("[%s %s] canceled after %zu bytes\n", a->name, a->key, total);
            break;
        }
        if (done) {
            printf("[%s %s] done, total=%zu bytes\n", a->name, a->key, total); 
            break;
        }
    }

    cache_release(acq.rec);
    free(a);
}

int main(void) {
    srand(1234);

    cache_t c;
    if (cache_init(&c, N_BUCKETS, SOFT_LIMIT_BYTES) != 0) {
        fprintf(stderr, "cache_init failed\n");
        return 1;
    }

    threadpool_t tp;
    if (tp_init(&tp, 4, 32) != 0) {
        fprintf(stderr, "tp_init failed\n");
        cache_destroy(&c);
        return 1;
    }

    const char *K0 = "http://test/0";
    const char *K1 = "http://test/1";

    writer_arg_t *w0 = malloc(sizeof *w0); *w0 = (writer_arg_t){ .c=&c, .key=K0, .chunks=12, .delay_ms=50 };
    writer_arg_t *w1 = malloc(sizeof *w1); *w1 = (writer_arg_t){ .c=&c, .key=K1, .chunks=10, .delay_ms=70 };
    tp_submit(&tp, writer_job, w0);
    tp_submit(&tp, writer_job, w1);

    usleep(30 * 1000);

    reader_arg_t *r0 = malloc(sizeof *r0); *r0 = (reader_arg_t){ .c=&c, .key=K0, .name="R0" };
    reader_arg_t *r1 = malloc(sizeof *r1); *r1 = (reader_arg_t){ .c=&c, .key=K1, .name="R1" };
    tp_submit(&tp, reader_job, r0);
    tp_submit(&tp, reader_job, r1);

    usleep(400 * 1000);
    reader_arg_t *r2 = malloc(sizeof *r2); *r2 = (reader_arg_t){ .c=&c, .key=K0, .name="R2-HIT" };
    tp_submit(&tp, reader_job, r2);

    tp_poison_and_join(&tp);
    tp_destroy(&tp);

    cache_destroy(&c);
    return 0;
}
