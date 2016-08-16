/*
 * Copyright (C) 2016 Theobroma Systems Design & Consulting GmbH
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    core_sync Synchronization
 * @brief       Recursive Mutex for thread synchronization
 * @{
 *
 * @file
 * @brief       RIOT synchronization API
 *
 * @author      Martin Elshuber <martin.elshuber@theobroma-systems.com>
 *
 * The recursive mutex implementation is inspired by the implementetaion of
 * Nick v. IJzendoorn <nijzendoorn@engineering-spirit.nl>
 * @see https://github.com/RIOT-OS/RIOT/pull/4529/files#diff-8f48e1b9ed7a0a48d0c686a87cc5084eR35
 *
 */

#include <stdio.h>
#include <inttypes.h>

#include "rmutex.h"
#include "thread.h"
#include "assert.h"

#define ENABLE_DEBUG    (0)
#include "debug.h"

void rmutex_lock(rmutex_t *rmutex)
{
    kernel_pid_t owner;

    /* try to lock the mutex */
    DEBUG("rmutex %" PRIi16" : trylock\n", thread_getpid());
    if (mutex_trylock(&rmutex->mutex) == 0) {
        DEBUG("rmutex %" PRIi16" : mutex already held\n", thread_getpid());
        /* mutex is already held
         *
         * Case 1: the current thread holds the mutex
         *     Invariant 1: holds
         *     rmutex->owner == thread_getpid()
         *
         * Case 2: another thread holds the mutex
         *     Invariant 1: holds
         *     rmutex->owner != thread_getpid()
         *
         * Note for Case 2:
         *
         *     The foreign thread with PID forgeign_pid might change
         *     rmutex->owner.
         *
         *     a) either from KERNEL_PID_UNDEF to any forgeign PID if
         *     it is currently executing rmutex_lock or rmutex_trylock
         *     even if this happens multiple times with different PIDs
         *     the outcome of the next if statement will not be
         *     influenced
         *
         *     b) either from forgeign_pid to KERNEL_PID_UNDEF if
         *     it is currently executing rmutex_unlock
         *
         *     but this does not bother the invariant
         */

        /* ensure that owner is read only once */
        owner = atomic_load_explicit( &rmutex->owner, memory_order_relaxed);
        DEBUG("rmutex %" PRIi16" : mutex held by %" PRIi16" \n", thread_getpid(), owner);

        /* Case 2 : Another Thread hold the mutex */
        if ( owner != thread_getpid() ) {
            /* wait for the mutex */
            DEBUG("rmutex %" PRIi16" : locking mutex\n", thread_getpid());

            mutex_lock(&rmutex->mutex);
        }
        /* Case 1 : We already hold the mutex */
        else {
            assert(rmutex->refcount>0);
        }
    }

    DEBUG("rmutex %" PRIi16" : I am now holding the mutex\n", thread_getpid());

    /* We are holding the recursive mutex */

    DEBUG("rmutex %" PRIi16" : settting the owner\n", thread_getpid());

    /* ensure that owner is written only once */
    atomic_store_explicit(&rmutex->owner, thread_getpid(), memory_order_relaxed);

    DEBUG("rmutex %" PRIi16" : increasing refs\n", thread_getpid());

    /* increase the refcount */
    rmutex->refcount++;
}

int rmutex_trylock(rmutex_t *rmutex)
{
    kernel_pid_t owner;

    /* try to lock the mutex */
    if (mutex_trylock(&rmutex->mutex) == 0) {
        /* ensure that owner is read only once */
        owner = atomic_load_explicit( &rmutex->owner, memory_order_relaxed);

        /* Case 2 : Another Thread hold the mutex */
        if ( owner != thread_getpid() ) {
            /* wait for the mutex */
            return 0;
        }
        /* Case 1 : We already hold the mutex */
        else {
            assert(rmutex->refcount>0);
        }
    }

    /* we are holding the recursive mutex */

    /* ensure that owner is written only once */
    atomic_store_explicit(&rmutex->owner, thread_getpid(), memory_order_relaxed);

    /* increase the refcount */
    rmutex->refcount++;
    return 1;
}

void rmutex_unlock(rmutex_t *rmutex)
{
    assert(atomic_load_explicit(&rmutex->owner,memory_order_relaxed) == thread_getpid());
    assert(rmutex->refcount > 0);

    DEBUG("rmutex %" PRIi16" : decrementing refs refs\n", thread_getpid());

    /* decrement refcount */
    rmutex->refcount--;

    /* check if we still hold the mutex */
    if (rmutex->refcount == 0) {
        /* if not release the mutex */

        DEBUG("rmutex %" PRIi16" : resetting owner\n", thread_getpid());

        /* ensure that owner is written only once */
        atomic_store_explicit(&rmutex->owner, KERNEL_PID_UNDEF, memory_order_relaxed);

        DEBUG("rmutex %" PRIi16" : releasing mutex\n", thread_getpid());

        mutex_unlock(&rmutex->mutex);
    }
}
