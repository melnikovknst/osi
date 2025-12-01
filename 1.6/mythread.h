#ifndef MYTHREAD_H
#define MYTHREAD_H
#include <sys/types.h>

typedef pid_t mythread_t;

int mythread_create(mythread_t *thr, void *(*start_routine)(void *), void *arg);

#endif
