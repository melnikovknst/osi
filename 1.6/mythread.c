#define _GNU_SOURCE
#include "mythread.h"

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

static _Atomic int active_threads = 0;

struct start_pack {
    void *(*fn)(void *);
    void  *arg;
    void **ret_slot;
};

struct watch {
    int    *ctid;
    void   *stack;
    size_t  stack_sz;
    void   *pack;
    struct watch *next;
};

static struct {
    int init;
    int lock;
    struct watch *head;
    void  *stack;
    size_t stack_sz;
    int tid;
} cleaner_struct;

static int tramp(void *p) {
    struct start_pack *sp = (struct start_pack *)p;

    void *rv = NULL;
    if (sp && sp->fn)
        rv = sp->fn(sp->arg);

    if (sp && sp->ret_slot)
       *sp->ret_slot = rv;

    atomic_fetch_sub_explicit(&active_threads, 1, memory_order_relaxed);

    syscall(SYS_exit, 0);
}

int mythread_create(mythread_t *thr, void *(*start_routine)(void *), void *arg) {
    if (!thr || !start_routine) {
        errno = EINVAL;
        return -1;
    }

    const size_t stk_sz = 1 << 20;
    void *stk = mmap(NULL, stk_sz,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                     -1, 0);
    if (stk == MAP_FAILED)
        return -1;

    int *ctid = (int*)malloc(sizeof *ctid);
    if (!ctid) {
        int e = errno;
        munmap(stk, stk_sz);
        errno = e;
        return -1;
    }
    *ctid = 1;

    struct start_pack *sp = (struct start_pack*)malloc(sizeof *sp);
    if (!sp) {
        int e = errno;
        munmap(stk, stk_sz);
        free(ctid);
        errno = e;
        return -1;
    }
    sp->fn = start_routine;
    sp->arg = arg;
    sp->ret_slot = &thr->retval;

    thr->stack = stk;
    thr->stack_sz = stk_sz;
    thr->ctid = ctid;
    thr->detached = 0;
    thr->pack = sp;
    thr->retval = NULL;

    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
              | CLONE_SYSVSEM | CLONE_THREAD
              | CLONE_CHILD_CLEARTID | CLONE_PARENT_SETTID;

    int tid = clone(tramp, (char*)stk + stk_sz, flags, sp, &thr->tid, NULL, ctid);

    if (tid == -1) {
        int e = errno;
        munmap(stk, stk_sz);
        free(ctid);
        free(sp);
        errno = e;
        return -1;
    }

    atomic_fetch_add_explicit(&active_threads, 1, memory_order_relaxed);

    return 0;
}

int mythread_join(mythread_t *thr, void **retval) {
    if (!thr || thr->detached) {
        errno = EINVAL;
        return -1;
    }
    while (1) {
        int v = *thr->ctid;
        if (v == 0) break;
        syscall(SYS_futex, thr->ctid, FUTEX_WAIT, v, NULL, NULL, 0);
    }

    if (retval)
       *retval = thr->retval;

    munmap(thr->stack, thr->stack_sz);
    free(thr->ctid);
    free(thr->pack);

    thr->stack = NULL;
    thr->stack_sz = 0;
    thr->ctid = NULL;
    thr->pack = NULL;
    thr->tid = 0;
    thr->retval = NULL;
    thr->detached = 1;

    return 0;
}

static int lock_acq(int *l) {
    while (1) {
        if (__sync_bool_compare_and_swap(l, 0, 1))
            return 0;
        syscall(SYS_futex, l, FUTEX_WAIT, 1, NULL, NULL, 0);
    }
}
static void lock_rel(int *l) {
    __sync_lock_release(l);
    syscall(SYS_futex, l, FUTEX_WAKE, 1, NULL, NULL, 0);
}

static int cleaner(void *arg) {
    (void)arg;
    while (1) {
        lock_acq(&cleaner_struct.lock);
        if (cleaner_struct.head == NULL && atomic_load_explicit(&active_threads, memory_order_relaxed) == 0) {
            cleaner_struct.init = 0;
            lock_rel(&cleaner_struct.lock);
            syscall(SYS_exit, 0);
        }
        struct watch **pp = &cleaner_struct.head;
        while (*pp) {
            struct watch *w = *pp;
            int v = *w->ctid;
            if (v != 0) {
                lock_rel(&cleaner_struct.lock);
                syscall(SYS_futex, w->ctid, FUTEX_WAIT, v, NULL, NULL, 0);
                lock_acq(&cleaner_struct.lock);
                continue;
            }
            *pp = w->next;
            munmap(w->stack, w->stack_sz);
            free(w->ctid);
            free(w->pack);
            free(w);
        }
        lock_rel(&cleaner_struct.lock);
        usleep(1000);
    }
    return -1;
}

static int cleaner_start(void) {
    if (!__sync_bool_compare_and_swap(&cleaner_struct.init, 0, 1))
        return 0;

    cleaner_struct.stack_sz = 1 << 20;
    cleaner_struct.stack = mmap(NULL, cleaner_struct.stack_sz, PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (cleaner_struct.stack == MAP_FAILED) {
        cleaner_struct.init = 0;
        return -1;
    }

    int flags = CLONE_VM | CLONE_FS | CLONE_FILES |
                CLONE_SIGHAND | CLONE_SYSVSEM | CLONE_THREAD;

    int tid = clone(cleaner, (char*)cleaner_struct.stack + cleaner_struct.stack_sz, flags, NULL);

    if (tid == -1) {
        int e = errno;
        munmap(cleaner_struct.stack, cleaner_struct.stack_sz);
        cleaner_struct.init = 0;
        errno = e;
        return -1;
    }
    cleaner_struct.tid = tid;
    return 0;
}


int mythread_detach(mythread_t *thr) {
    if (!thr) {
        errno = EINVAL;
        return -1;
    }
    if (thr->detached)
        return 0;

    thr->detached = 1;

    if (thr->pack) {
        ((struct start_pack*)thr->pack)->ret_slot = NULL;
    }

    if (*thr->ctid == 0) {
        munmap(thr->stack, thr->stack_sz);
        free(thr->ctid);
        free(thr->pack);
    } else {
        if (cleaner_start() == -1)
            return -1;

        struct watch *w = (struct watch*)malloc(sizeof *w);
        if (!w) {
            while (1) {
                int v = *thr->ctid;
                if (v == 0) break;
                syscall(SYS_futex, thr->ctid, FUTEX_WAIT, v, NULL, NULL, 0);
            }
        
            munmap(thr->stack, thr->stack_sz);
            free(thr->ctid);
            free(thr->pack);
        
            thr->stack = NULL;
            thr->stack_sz = 0;
            thr->ctid = NULL;
            thr->pack = NULL;
            thr->tid = 0;
            thr->retval = NULL;
        
            return -1;
        }

        w->ctid = thr->ctid;
        w->stack = thr->stack;
        w->stack_sz = thr->stack_sz;
        w->pack = thr->pack;

        lock_acq(&cleaner_struct.lock);
        w->next = cleaner_struct.head;
        cleaner_struct.head = w;
        lock_rel(&cleaner_struct.lock);
    }

    thr->stack = NULL;
    thr->stack_sz = 0;
    thr->ctid = NULL;
    thr->pack = NULL;
    thr->tid = 0;
    thr->retval = NULL;

    return 0;
}
