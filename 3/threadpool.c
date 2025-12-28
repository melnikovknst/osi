#include "threadpool.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int jq_init(job_queue_t *q, int cap) {
    q->buf = (job_t*)calloc(cap, sizeof(job_t));
    if(!q->buf) 
        return -1;

    q->cap = cap; q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return 0;
}

void jq_destroy(job_queue_t *q) {
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->m);

    free(q->buf);
}

int jq_push(job_queue_t *q, job_t j) {
    pthread_mutex_lock(&q->m);
    while(q->count == q->cap)
        pthread_cond_wait(&q->not_full, &q->m);

    q->buf[q->tail] = j;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;

    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->m);
    
    return 0;
}

int jq_pop(job_queue_t *q, job_t *j) {
    pthread_mutex_lock(&q->m);
    while(q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->m);

    *j = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->m);

    return 0;
}

static void* worker(void *arg) {
    threadpool_t *tp=(threadpool_t*)arg;
    while(1) {
        job_t j; 
        jq_pop(&tp->q, &j);
        if (j.poison)
            break;

        j.fn(j.arg);
    }

    return NULL;
}

int tp_init(threadpool_t *tp, int nworkers, int qcap) {
    tp->nworkers = nworkers;
    tp->th = (pthread_t*)calloc(nworkers, sizeof(pthread_t));

    if(!tp->th) 
        return -1;

    if(jq_init(&tp->q, qcap)) {
        free(tp->th); 
        return -1;
    }

    for(int i = 0; i < nworkers; i++)
        pthread_create(&tp->th[i], NULL, worker, tp);

    return 0;
}

void tp_destroy(threadpool_t *tp) {
    jq_destroy(&tp->q);
    free(tp->th);
}

int tp_submit(threadpool_t *tp, job_fn fn, void *arg) {
    job_t j;
    j.fn=fn;
    j.arg=arg;
    j.poison = 0;

    return jq_push(&tp->q, j);
}

void tp_poison_and_join(threadpool_t *tp) {
    for(int i = 0; i < tp->nworkers; i++) {
        job_t j;
        j.poison = 1;
        jq_push(&tp->q, j);
    }
    
    for(int i = 0; i < tp->nworkers; i++)
        pthread_join(tp->th[i], NULL);
}
