#define _GNU_SOURCE
#include "uthread.h"

#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>

static ucontext_t sched_ctx;
static uthread_t *runq_head;
static uthread_t *runq_tail;
static uthread_t *current;
static int sched_inited;

static _Atomic int active_threads = 0;

struct start_pack {
	void *(*fn)(void *);
	void *arg;
	void **ret_slot;
	uthread_t *t;
};

static void enqueue(uthread_t *t) {
	t->next = NULL;
	if (!runq_tail) {
        runq_head = runq_tail = t; 
        return;
    }
	runq_tail->next = t;
	runq_tail = t;
}

static uthread_t* dequeue(void) {
	uthread_t *t = runq_head;
	if (!t) return NULL;
	runq_head = t->next;
	if (!runq_head) runq_tail = NULL;
	t->next = NULL;
	return t;
}

static void starter(uintptr_t p_hi, uintptr_t p_lo) {
	uintptr_t p = (p_hi << 32) | p_lo;
	struct start_pack *sp = (struct start_pack*)(uintptr_t)p;
	void *rv = NULL;
	if (sp && sp->fn) {
        rv = sp->fn(sp->arg);
    }

	if (sp && sp->ret_slot) {
        *sp->ret_slot = rv;
    }

	sp->t->retval = rv;
	sp->t->finished = 1;
	free(sp);
	atomic_fetch_sub_explicit(&active_threads, 1, memory_order_relaxed);
	current = NULL;
	setcontext(&sched_ctx);
}

int uthread_create(uthread_t *thr, void *(*start_routine)(void *), void *arg) {
	if (!thr || !start_routine) {
        errno = EINVAL;
        return -1;
    }

	if (!sched_inited) {
		getcontext(&sched_ctx);
		sched_inited = 1;
	}
	const size_t stk_sz = 1<<20;
	void *stk = mmap(NULL, stk_sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
	if (stk == MAP_FAILED) {
        return -1;
    }

	struct start_pack *sp = (struct start_pack*)malloc(sizeof *sp);
	if (!sp) {
        int e=errno;
        munmap(stk, stk_sz);
        errno=e;
        return -1;
    }

	memset(thr, 0, sizeof *thr);
	thr->stack = stk;
	thr->stack_sz = stk_sz;

	getcontext(&thr->ctx);
	thr->ctx.uc_stack.ss_sp = stk;
	thr->ctx.uc_stack.ss_size = stk_sz;
	thr->ctx.uc_link = &sched_ctx;

	sp->fn = start_routine;
	sp->arg = arg;
	sp->ret_slot = &thr->retval;
	sp->t = thr;

	uintptr_t up = (uintptr_t)sp;
	uintptr_t hi = (up >> 32) & 0xffffffffu;
	uintptr_t lo = up & 0xffffffffu;
	makecontext(&thr->ctx, (void(*)())starter, 2, (uintptr_t)hi, (uintptr_t)lo);

	enqueue(thr);
	atomic_fetch_add_explicit(&active_threads, 1, memory_order_relaxed);

	return 0;
}

static void sched_once(void) {
	uthread_t *t = dequeue();
	if (!t) {
    return;
    }

	current = t;
	swapcontext(&sched_ctx, &t->ctx);

	if (t->finished && !t->joined) {
        enqueue(t);
    }

	current = NULL;
}

int uthread_join(uthread_t *thr, void **retval) {
	if (!thr || thr->joined) {
        errno = EINVAL;
        return -1;
    }
	while (!thr->finished) {
		sched_once();
	}

	thr->joined = 1;

	if (retval) {
        *retval = thr->retval;
    }

	munmap(thr->stack, thr->stack_sz);
	thr->stack = NULL;
	thr->stack_sz = 0;
	thr->retval = NULL;

	return 0;
}

// static int cleaner(void *arg) {
//     (void)arg;
//     while (1) {
//         lock_acq(&cleaner_struct.lock);
//         if (cleaner_struct.head == NULL && atomic_load_explicit(&active_threads, memory_order_relaxed) == 0) {
//             cleaner_struct.init = 0;
//             lock_rel(&cleaner_struct.lock);
//             syscall(SYS_exit, 0);
//         }
//         struct watch **pp = &cleaner_struct.head;
//         while (*pp) {
//             struct watch *w = *pp;
//             int v = *w->ctid;
//             if (v != 0) {
//                 lock_rel(&cleaner_struct.lock);
//                 syscall(SYS_futex, w->ctid, FUTEX_WAIT, v, NULL, NULL, 0);
//                 lock_acq(&cleaner_struct.lock);
//                 continue;
//             }
//             *pp = w->next;
//             munmap(w->stack, w->stack_sz);
//             free(w->ctid);
//             free(w->pack);
//             free(w);
//         }
//         lock_rel(&cleaner_struct.lock);
//         usleep(1000);
//     }
//     return -1;
// }

// static int cleaner_start(void) {
//     if (!__sync_bool_compare_and_swap(&cleaner_struct.init, 0, 1))
//         return 0;

//     cleaner_struct.stack_sz = 1 << 20;
//     cleaner_struct.stack = mmap(NULL, cleaner_struct.stack_sz, PROT_READ | PROT_WRITE, 
//         MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
//     if (cleaner_struct.stack == MAP_FAILED) {
//         cleaner_struct.init = 0;
//         return -1;
//     }

//     int flags = CLONE_VM | CLONE_FS | CLONE_FILES |
//                 CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_THREAD;

//     int tid = clone(cleaner, (char*)cleaner_struct.stack + cleaner_struct.stack_sz, flags, NULL);

//     if (tid == -1) {
//         int e = errno;
//         munmap(cleaner_struct.stack, cleaner_struct.stack_sz);
//         cleaner_struct.init = 0;
//         errno = e;
//         return -1;
//     }
//     cleaner_struct.tid = tid;
//     return 0;
// }

// static int lock_acq(int *l) {
// 	while (1) {
// 		if (__sync_bool_compare_and_swap(l, 0, 1))
// 			return 0;
// 		syscall(SYS_futex, l, FUTEX_WAIT, 1, NULL, NULL, 0);
// 	}
// }
// static void lock_rel(int *l) {
// 	__sync_lock_release(l);
// 	syscall(SYS_futex, l, FUTEX_WAKE, 1, NULL, NULL, 0);
// }

// static int cleaner_start(void) {
//     if (!__sync_bool_compare_and_swap(&cleaner_struct.init, 0, 1))
//         return 0;

//     cleaner_struct.stack_sz = 1 << 20;
//     cleaner_struct.stack = mmap(NULL, cleaner_struct.stack_sz, PROT_READ | PROT_WRITE, 
//         MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
//     if (cleaner_struct.stack == MAP_FAILED) {
//         cleaner_struct.init = 0;
//         return -1;
//     }

//     int flags = CLONE_VM | CLONE_FS | CLONE_FILES |
//                 CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_THREAD;

//     int tid = clone(cleaner, (char*)cleaner_struct.stack + cleaner_struct.stack_sz, flags, NULL);

//     if (tid == -1) {
//         int e = errno;
//         munmap(cleaner_struct.stack, cleaner_struct.stack_sz);
//         cleaner_struct.init = 0;
//         errno = e;
//         return -1;
//     }
//     cleaner_struct.tid = tid;
//     return 0;
// }

// int mythread_detach(mythread_t *thr) {
//     if (!thr) {
//         errno = EINVAL;
//         return -1;
//     }
//     if (thr->detached)
//         return 0;

//     thr->detached = 1;

//     if (thr->pack) {
//         ((struct start_pack*)thr->pack)->ret_slot = NULL;
//     }

//     if (*thr->ctid == 0) {
//         munmap(thr->stack, thr->stack_sz);
//         free(thr->ctid);
//         free(thr->pack);
//     } else {
//         if (cleaner_start() == -1)
//             return -1;

//         struct watch *w = (struct watch*)malloc(sizeof *w);
//         if (!w)
//             return -1;

//         w->ctid = thr->ctid;
//         w->stack = thr->stack;
//         w->stack_sz = thr->stack_sz;
//         w->pack = thr->pack;

//         lock_acq(&cleaner_struct.lock);
//         w->next = cleaner_struct.head;
//         cleaner_struct.head = w;
//         lock_rel(&cleaner_struct.lock);
//     }

//     thr->stack = NULL;
//     thr->stack_sz = 0;
//     thr->ctid = NULL;
//     thr->pack = NULL;
//     thr->tid = 0;
//     thr->retval = NULL;

//     return 0;
// }


void uthread_yield(void) {
	if (!current) return;
	enqueue(current);
	uthread_t *me = current;
	current = NULL;
	swapcontext(&me->ctx, &sched_ctx);
}
