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

static int list_size = 0;

static void *counter_thread(void *arg) {
	worker_arg_t *warg = (worker_arg_t *)arg;
	Storage *st = warg->st;
	int type = warg->type;

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

		while (1) {
			pthread_spin_lock(&cur->sync);
			Node *next = cur->next;

			if (!next) {
				pthread_spin_unlock(&cur->sync);
				break;
			}

			pthread_spin_lock(&next->sync);

			int len1 = (int)strlen(cur->value);
			int len2 = (int)strlen(next->value);

			if (type == WORKER_INC && len1 < len2)
				local_pairs++;
			else if (type == WORKER_DEC && len1 > len2)
				local_pairs++;
			else if (type == WORKER_EQ && len1 == len2)
				local_pairs++;

			pthread_spin_unlock(&cur->sync);
			pthread_spin_unlock(&next->sync);

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

static void *swap_thread(void *arg) {
	worker_arg_t *warg = (worker_arg_t *)arg;
	Storage *st = warg->st;
	int type = warg->type;
	unsigned int seed = (unsigned int)(time(NULL) ^ gettid());

	while (1) {
		if (list_size < 3)
			continue;

		int idx = rand_r(&seed) % (list_size - 2);

		Node *prev = st->first;
		if (!prev)
			continue;

		pthread_spin_lock(&prev->sync);

		int pos = 0;
		while (pos < idx) {
			Node *tmp = prev->next;
			if (!tmp) {
				pthread_spin_unlock(&prev->sync);
				prev = NULL;
				break;
			}
			pthread_spin_lock(&tmp->sync);
			pthread_spin_unlock(&prev->sync);
			prev = tmp;
			pos++;
		}

		if (!prev)
			continue;

		Node *cur = prev->next;
		if (!cur) {
			pthread_spin_unlock(&prev->sync);
			continue;
		}
		pthread_spin_lock(&cur->sync);

		Node *next = cur->next;
		if (!next) {
			pthread_spin_unlock(&cur->sync);
			pthread_spin_unlock(&prev->sync);
			continue;
		}
		pthread_spin_lock(&next->sync);

		int len2 = (int)strlen(cur->value);
		int len3 = (int)strlen(next->value);
		int need_swap = 0;

		if (type == WORKER_INC && len2 > len3)
			need_swap = 1;
		else if (type == WORKER_DEC && len2 < len3)
			need_swap = 1;
		else if (type == WORKER_EQ && len2 != len3)
			need_swap = 1;

		if (need_swap) {
			Node *tail = next->next;
			prev->next = next;
			next->next = cur;
			cur->next = tail;

			if (type == WORKER_INC)
				atomic_fetch_add(&swaps_inc, 1);
			else if (type == WORKER_DEC)
				atomic_fetch_add(&swaps_dec, 1);
			else
				atomic_fetch_add(&swaps_eq, 1);
		}

		pthread_spin_unlock(&next->sync);
		pthread_spin_unlock(&cur->sync);
		pthread_spin_unlock(&prev->sync);
	}

	return NULL;
}

static void *monitor_thread(void *arg) {
	(void)arg;

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
	int n = 1000000;
	int err;
	list_size = n;

	Storage *st = storage_init(n);

	pthread_t tid_counter_inc;
	pthread_t tid_counter_dec;
	pthread_t tid_counter_eq;

	pthread_t tid_swap_inc;
	pthread_t tid_swap_dec;
	pthread_t tid_swap_eq;

	pthread_t tid_mon;

	worker_arg_t arg_counter_inc;
	worker_arg_t arg_counter_dec;
	worker_arg_t arg_counter_eq;

	worker_arg_t arg_swap_inc;
	worker_arg_t arg_swap_dec;
	worker_arg_t arg_swap_eq;

	arg_counter_inc.st = st;
	arg_counter_inc.type = WORKER_INC;

	arg_counter_dec.st = st;
	arg_counter_dec.type = WORKER_DEC;

	arg_counter_eq.st = st;
	arg_counter_eq.type = WORKER_EQ;

	arg_swap_inc.st = st;
	arg_swap_inc.type = WORKER_INC;

	arg_swap_dec.st = st;
	arg_swap_dec.type = WORKER_DEC;

	arg_swap_eq.st = st;
	arg_swap_eq.type = WORKER_EQ;

	err = pthread_create(&tid_counter_inc, NULL, counter_thread, &arg_counter_inc);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_counter_dec, NULL, counter_thread, &arg_counter_dec);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_counter_eq, NULL, counter_thread, &arg_counter_eq);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_swap_inc, NULL, swap_thread, &arg_swap_inc);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_swap_dec, NULL, swap_thread, &arg_swap_dec);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_swap_eq, NULL, swap_thread, &arg_swap_eq);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	err = pthread_create(&tid_mon, NULL, monitor_thread, NULL);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}

	pthread_join(tid_counter_inc, NULL);
	pthread_join(tid_counter_dec, NULL);
	pthread_join(tid_counter_eq, NULL);
	pthread_join(tid_swap_inc, NULL);
	pthread_join(tid_swap_dec, NULL);
	pthread_join(tid_swap_eq, NULL);
	pthread_join(tid_mon, NULL);

	return 0;
}