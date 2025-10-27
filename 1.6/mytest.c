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
    sleep(5);
    write(1, "detached thr: done\n", 22);
    return NULL;
}

int main(void) {
    mythread_t tj, td;
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
        fprintf(stderr, "detach: %s\n", strerror(errno));
        return 1;
    }

    if (mythread_join(&tj, &ret)) {
        fprintf(stderr, "join: %s\n", strerror(errno));
        return 1;
    }
    printf("join ret val = %d\n", (int)(intptr_t)ret);

    syscall(SYS_exit, 0);
}
