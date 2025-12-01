#define _GNU_SOURCE
#include "mythread.h"
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

struct start_pack {
    void *(*fn)(void *);
    void *arg;
};

static int tramp(void *p) {
    struct start_pack *sp = (struct start_pack *)p;
    void *(*fn)(void*) = sp->fn;
    void *arg = sp->arg;
    free(sp);
    (void)fn(arg);
    syscall(SYS_exit, 0);
    return 0;
}

int mythread_create(mythread_t *thr, void *(*start_routine)(void *), void *arg) {
    const size_t stack_sz = 1 << 20;
    void *stk = mmap(NULL, stack_sz, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (stk == MAP_FAILED) return -1;

    struct start_pack *sp = malloc(sizeof(*sp));
    if (!sp) { munmap(stk, stack_sz); errno = ENOMEM; return -1; }
    sp->fn = start_routine;
    sp->arg = arg;

    int flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND
              | CLONE_SYSVSEM | CLONE_THREAD;

    int tid = clone(tramp, (char*)stk + stack_sz, flags, sp);
    if (tid == -1) {
        int e = errno;
        munmap(stk, stack_sz);
        free(sp);
        errno = e;
        return -1;
    }

    if (thr) *thr = tid;
    return 0;
}
