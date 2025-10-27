#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/syscall.h>
#include "mythread.h"

static void *worker_join(void *arg) {
    int x = (int)(intptr_t)arg;
    x++;
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
    mythread_t tj, td, td2;
    void *ret = NULL;

    if (mythread_create(&tj, worker_join, (void*)(intptr_t)5)) {
        fprintf(stderr, "create(join): %s\n", strerror(errno));
        return 1;
    }

    if (mythread_create(&td, worker_detach, NULL)) {
        fprintf(stderr, "create(detach): %s\n", strerror(errno));
        return 1;
    }
    if (mythread_detach(&td)) {
        fprintf(stderr, "detach 1: %s\n", strerror(errno));
        return 1;
    }

    if (mythread_create(&td2, worker_detach_2, NULL)) {
        fprintf(stderr, "create(detach): %s\n", strerror(errno));
        return 1;
    }
    if (mythread_detach(&td2)) {
        fprintf(stderr, "detach 2: %s\n", strerror(errno));
        return 1;
    }

    if (mythread_join(&tj, &ret)) {
        fprintf(stderr, "join: %s\n", strerror(errno));
        return 1;
    }
    printf("join ret val = %d\n", (int)(intptr_t)ret);

    sleep(2);

    syscall(SYS_exit, 0);
}
