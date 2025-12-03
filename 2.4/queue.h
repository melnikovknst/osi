#pragma once

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>


typedef struct _QueueNode {
    int val;
    struct _QueueNode *next;
} qnode_t;

typedef struct _Queue {
    qnode_t *first;
    qnode_t *last;

    pthread_t qmonitor_tid;

    int count;
    int max_count;

    long add_attempts;
    long get_attempts;
    long add_count;
    long get_count;

    pthread_spinlock_t lock;

    sem_t slots_free;       
    sem_t slots_used;       
} queue_t;

queue_t* queue_init(int max_count);
void queue_destroy(queue_t *q);
int  queue_add(queue_t *q, int val);
int  queue_get(queue_t *q, int *val);
void queue_print_stats(queue_t *q);
