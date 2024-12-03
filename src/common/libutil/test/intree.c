/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/intree.h"

#define NTHREADS 16

/* pthread_barrier_t is an optional POSIX feature and is not present
 * on macos.  If unavailable, a simple replacement is provided below.
 */
#if !defined _POSIX_BARRIERS || _POSIX_BARRIERS <= 0

typedef struct {
    int max;
    int count;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} pthread_barrier_t;

#ifndef PTHREAD_BARRIER_SERIAL_THREAD
#define PTHREAD_BARRIER_SERIAL_THREAD -1
#endif

int pthread_barrier_destroy (pthread_barrier_t *barrier)
{
    if (!barrier)
        return EINVAL;
    pthread_mutex_destroy (&barrier->lock);
    pthread_cond_destroy (&barrier->cond);
    return 0;
}

int pthread_barrier_init (pthread_barrier_t *barrier,
                          void *barrier_attr,
                          unsigned count)
{
    if (!barrier || barrier_attr != NULL)
        return EINVAL;
    barrier->max = count;
    barrier->count = 0;
    pthread_mutex_init (&barrier->lock, NULL);
    pthread_cond_init (&barrier->cond, NULL);
    return 0;
}

int pthread_barrier_wait (pthread_barrier_t *barrier)
{
    int ret = EINVAL;
    if (barrier) {
        pthread_mutex_lock (&barrier->lock);
        if (++barrier->count == barrier->max) {
            pthread_cond_broadcast (&barrier->cond);
            ret = PTHREAD_BARRIER_SERIAL_THREAD;
        }
        else if (barrier->count < barrier->max) {
            pthread_cond_wait (&barrier->cond, &barrier->lock);
            ret = 0;
        }
        pthread_mutex_unlock (&barrier->lock);
    }
    return ret;
}
#endif

static pthread_barrier_t barrier;

static void *thd_intree (void *arg)
{
    int *resultp = arg;
    int e;
    if ((e = pthread_barrier_wait (&barrier))
        && e != PTHREAD_BARRIER_SERIAL_THREAD)
        BAIL_OUT ("pthread_barrier_wait: %s %s", strerror (e), strerror (errno));
    *resultp = executable_is_intree ();
    return NULL;
}

int main (int argc, char *argv[])
{
    pthread_t threads [NTHREADS];
    int results [NTHREADS];
    int i;
    plan (NO_PLAN);

    ok (executable_is_intree (),
        "executable_is_intree() works");
    like (executable_selfdir (), ".*/src/common/libutil",
        "executable_selfdir() works");

    if (pthread_barrier_init (&barrier, NULL, NTHREADS))
        BAIL_OUT ("pthread_barrier_init");

    for (i = 0; i < NTHREADS; i++) {
        int e = pthread_create (&threads[i], NULL, thd_intree, &results[i]);
        if (e)
            BAIL_OUT ("pthread_create");
    }
    int pass = 1;
    for (i = 0; i < NTHREADS; i++) {
        if (pthread_join (threads[i], NULL))
            BAIL_OUT ("pthread_join");
        if (results[i] != 1) {
            pass = 0;
            fail ("thread %d: executable_is_intree() returned %d",
                  i, results[i]);
        }
    }

    ok (pass == 1,
        "%d threads ran executable_is_intree successfully", NTHREADS);

    pthread_barrier_destroy (&barrier);
    done_testing ();
    return 0;
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
