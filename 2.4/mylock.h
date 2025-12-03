#pragma once

#define _GNU_SOURCE
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>

typedef struct {
    int val;        // 0 - unlocked / 1 - locked
} my_spinlock_t;

static void my_spin_init(my_spinlock_t *l) {
    l->val = 0;
}

static void my_spin_lock(my_spinlock_t *l) {
    while (1) {
        if (__sync_val_compare_and_swap(&l->val, 0, 1) == 0)
            return;
    }
}

static void my_spin_unlock(my_spinlock_t *l) {
    __sync_lock_release(&l->val);
}
