#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "list.h"

enum {
	WORKER_INC = 0,
	WORKER_DEC = 1,
	WORKER_EQ  = 2
};

typedef struct { 
	Storage *st;
	int type;
} worker_arg_t;

static _Atomic long iters_inc = 0;
static _Atomic long iters_dec = 0;
static _Atomic long iters_eq = 0;

static _Atomic long swaps_inc = 0;
static _Atomic long swaps_dec = 0;
static _Atomic long swaps_eq = 0;

static void *worker_thread(void *arg) {
	worker_arg_t *warg = (worker_arg_t *)arg;
	Storage *st = warg->st;
	int type = warg->type;
	unsigned int seed = (unsigned int)(time(NULL) ^ gettid());

	while (1) {
		long local_pairs = 0;
		Node *cur = st->first;

		if (!cur) {
			if (type == WORKER_INC)
				atomic_fetch_add(&iters_inc, 1);
			else if (type == WORKER_DEC)
				atomic_fetch_add(&iters_dec, 1);
			else
				atomic_fetch_add(&iters_eq, 1);
			continue;
		}

		pthread_mutex_lock(&cur->sync);

		while (1) {
			Node *next = cur->next;
			if (!next) {
				pthread_mutex_unlock(&cur->sync);
				break;
			}

			pthread_mutex_lock(&next->sync);

			int len1 = (int)strlen(cur->value);
			int len2 = (int)strlen(next->value);

			if (type == WORKER_INC && len1 < len2)
				local_pairs++;
			else if (type == WORKER_DEC && len1 > len2)
				local_pairs++;
			else if (type == WORKER_EQ && len1 == len2)
				local_pairs++;

			Node *third = next->next;
            if (third && rand_r(&seed) % 2 == 0) {
                pthread_mutex_lock(&third->sync);

                int len3 = (int)strlen(third->value);
                int need_swap = 0;

                if (type == WORKER_INC && len2 > len3)
                    need_swap = 1;
                else if (type == WORKER_DEC && len2 < len3)
                    need_swap = 1;
                else if (type == WORKER_EQ && len2 == len3)
                    need_swap = 1;

                if (need_swap) {
                    Node *tail = third->next;
                    cur->next = third;
                    third->next = next;
                    next->next = tail;

                    if (type == WORKER_INC)
                        atomic_fetch_add(&swaps_inc, 1);
                    else if (type == WORKER_DEC)
                        atomic_fetch_add(&swaps_dec, 1);
                    else
                        atomic_fetch_add(&swaps_eq, 1);
                }

                pthread_mutex_unlock(&third->sync);
            }
        

			pthread_mutex_unlock(&cur->sync);
            pthread_mutex_unlock(&next->sync);
			cur = next;
		}

		if (type == WORKER_INC)
			atomic_fetch_add(&iters_inc, 1);
		else if (type == WORKER_DEC)
			atomic_fetch_add(&iters_dec, 1);
		else
			atomic_fetch_add(&iters_eq, 1);
	}

	return NULL;
}

static void *monitor_thread() {

	while (1) {
		sleep(1);

		long ii = atomic_load(&iters_inc);
		long id = atomic_load(&iters_dec);
		long ie = atomic_load(&iters_eq);

		long si = atomic_load(&swaps_inc);
		long sd = atomic_load(&swaps_dec);
		long se = atomic_load(&swaps_eq);

		printf("monitor: iters [inc=%ld dec=%ld eq=%ld]; swaps [inc=%ld dec=%ld eq=%ld]\n",
			ii, id, ie, si, sd, se);
	}

	return NULL;
}

int main() {
	int n = 1000;
	int err;

	srand(time(NULL));

	Storage *st = storage_init(n);

	pthread_t tid_inc;
	pthread_t tid_dec;
	pthread_t tid_eq;
	pthread_t tid_mon;

	worker_arg_t arg_inc;
	worker_arg_t arg_dec;
	worker_arg_t arg_eq;

	arg_inc.st = st;
	arg_inc.type = WORKER_INC;

	arg_dec.st = st;
	arg_dec.type = WORKER_DEC;

	arg_eq.st = st;
	arg_eq.type = WORKER_EQ;

	err = pthread_create(&tid_inc, NULL, worker_thread, &arg_inc);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_dec, NULL, worker_thread, &arg_dec);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_eq, NULL, worker_thread, &arg_eq);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_mon, NULL, monitor_thread, NULL);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	pthread_join(tid_inc, NULL);
	pthread_join(tid_dec, NULL);
	pthread_join(tid_eq, NULL);
	pthread_join(tid_mon, NULL);

	return 0;
}