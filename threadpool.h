#pragma once
#include <pthread.h>
#include <stdint.h>

typedef void (*job_fn)(void *arg);

typedef struct {
    job_fn fn;
    void *arg;
    int poison;
} job_t;

typedef struct {
    job_t *buf;
    int cap, head, tail, count;
    pthread_mutex_t m;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} job_queue_t;

typedef struct {
    int nworkers;
    pthread_t *th;
    job_queue_t q;
} threadpool_t;

int  jq_init(job_queue_t *q, int cap);
void jq_destroy(job_queue_t *q);
int  jq_push(job_queue_t *q, job_t j);
int  jq_pop(job_queue_t *q, job_t *j);

int  tp_init(threadpool_t *tp, int nworkers, int qcap);
void tp_destroy(threadpool_t *tp);
int  tp_submit(threadpool_t *tp, job_fn fn, void *arg);
void tp_poison_and_join(threadpool_t *tp);
