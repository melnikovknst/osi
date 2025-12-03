#pragma once

#define _GNU_SOURCE
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>

typedef struct {
    int state;        // 0 - unlocked / 1 - locked
} my_spinlock_t;

static void my_spin_init(my_spinlock_t *l) {
    l->state = 0;
}

static void my_spin_lock(my_spinlock_t *l) {
    while (1) {
        if (__sync_val_compare_and_swap(&l->state, 0, 1) == 0)
            return;
    }
}

static void my_spin_unlock(my_spinlock_t *l) {
    __sync_lock_release(&l->state);
}



// mutex

typedef struct {
    int state;  // 0 - unlocked / 1 - locked
} my_mutex_t;

static void my_mutex_init(my_mutex_t *m) {
    m->state = 0;
}

static int futex_wait_int(int *addr, int expected) {
    return syscall(SYS_futex, addr, FUTEX_WAIT, expected, NULL, NULL, 0);
}

static int futex_wake_int(int *addr, int n) {
    return syscall(SYS_futex, addr, FUTEX_WAKE, n, NULL, NULL, 0);
}

static inline void my_mutex_lock(my_mutex_t *m) {
    int s;

    while (1) {
        s = m->state;

        if (s == 0 && __sync_val_compare_and_swap(&m->state, 0, 1) == 0)
            return;

        if (s == 1) {
            if (__sync_val_compare_and_swap(&m->state, 1, 2) != 1)
                continue;
            
            s = 2;
        }

        if (s == 2) {
            int rc = futex_wait_int(&m->state, 2);
            if (rc == -1) 
                perror("futex_wait");
        }
    }
}

static inline void my_mutex_unlock(my_mutex_t *m) {

    int s = __sync_fetch_and_sub(&m->state, 1);

    if (s == 1) 
        return;

    m->state = 0;
    int rc = futex_wake_int(&m->state, 1);
    if (rc == -1) {
        perror("futex_wake");
    }
}

