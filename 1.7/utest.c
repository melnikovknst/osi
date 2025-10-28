#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include "uthread.h"

static void *worker_join_1(void *arg) {
	int x = (int)(intptr_t)arg;
    uthread_yield();
	x++;
    write(1, "joined thr 1: done\n", 19);
	return (void*)(intptr_t)x;
}

static void *worker_join_2(void *arg) {
	int x = (int)(intptr_t)arg;
	x++;
    write(1, "joined thr 2: done\n", 19);
	return (void*)(intptr_t)x;
}

static void *worker_detach(void *arg) {
	(void)arg;
	write(1, "detached thr 1: done\n", 21);
	return NULL;
}

static void *worker_detach_2(void *arg) {
	(void)arg;
	sleep(7);
	write(1, "detached thr 2: done\n", 21);
	return NULL;
}

int main(void) {
	uthread_t tj1, tj2, td, td2;
	void *ret1 = NULL;
    void *ret2 = NULL;

	if (uthread_create(&tj1, worker_join_1, (void*)(intptr_t)5)) {
		fprintf(stderr, "create(join): %s\n", strerror(errno));
		return 1;
	}

    if (uthread_create(&tj2, worker_join_2, (void*)(intptr_t)3)) {
		fprintf(stderr, "create(join): %s\n", strerror(errno));
		return 1;
	}

    if (uthread_join(&tj1, &ret1)) {
		fprintf(stderr, "join: %s\n", strerror(errno));
		return 1;
	}

    printf("join 1 ret val = %d\n", (int)(intptr_t)ret1);
    fflush(stdout);

	if (uthread_join(&tj2, &ret2)) {
		fprintf(stderr, "join: %s\n", strerror(errno));
		return 1;
	}
	printf("join 2 ret val = %d\n", (int)(intptr_t)ret2);
    fflush(stdout);

	sleep(2);
	syscall(SYS_exit, 0);
}
