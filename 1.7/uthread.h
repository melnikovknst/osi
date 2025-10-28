#ifndef UTHREAD_H
#define UTHREAD_H
#include <sys/types.h>
#include <stddef.h>
#include <ucontext.h>

typedef struct uthread {
	ucontext_t ctx;
	void *stack;
	size_t stack_sz;
	int finished;
	void *retval;
	int joined;
	struct uthread *next;
} uthread_t;

int uthread_create(uthread_t *thr, void *(*start_routine)(void *), void *arg);
int uthread_join(uthread_t *thr, void **retval);

void uthread_yield(void);

#endif
