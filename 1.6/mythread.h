#ifndef MYTHREAD_H
#define MYTHREAD_H
#include <sys/types.h>
#include <stddef.h>

typedef struct {
    int      tid;
    void    *stack;
    size_t   stack_sz;
    int     *ctid;
    int      detached;
    void    *pack;
    void    *retval;
} mythread_t;

int mythread_create(mythread_t *thr, void *(*start_routine)(void *), void *arg);
int mythread_join(mythread_t *thr, void **retval);
int mythread_detach(mythread_t *thr);

#endif
