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
