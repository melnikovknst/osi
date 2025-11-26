#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "threadpool.h"

static void worker(void *arg) {
    printf("worker %lu started (%d ms)\n", (unsigned long)pthread_self(), *(int*)arg);
    sleep(*(int*)arg);
    printf("worker %lu finished\n", (unsigned long)pthread_self());
}

int main() {
    int W = 4, Q = 32, N = 20;

    threadpool_t tp;
    if (tp_init(&tp, W, Q) != 0) {
        fprintf(stderr, "tp_init failed\n");
        return 1;
    }

    int arg = 10;
    for (int i = 0; i < N; i++) {
        if (tp_submit(&tp, worker, &arg) != 0) {
            fprintf(stderr, "tp_submit failed\n");
        }
        printf("submited %d\n", i);
    }

    sleep(20);

    tp_poison_and_join(&tp);
    tp_destroy(&tp);
    
    return 0;
}
