#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "mythread.h"

static void *work(void *arg) {
    (void)arg;
    write(1, "hello from mythread\n", 21);
    return NULL;
}

int main(void) {
    mythread_t t;
    int err = mythread_create(&t, work, NULL);
    if (err) {
        fprintf(stderr, "mythread_create: %s\n", strerror(errno));
        return 1;
    }
    sleep(1);
    return 0;
}