/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* tkvswatch.c - exercise kvs watch functions */

/* Usage ./tkvswatch nthreads changes key
 * Spawn 'nthreads' threads each watching the same value.
 * Change it 'changes' times and ensure that at minimum the last
 * value is read.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <czmq.h>
#include <libgen.h>
#include <pthread.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#define USE_REACTOR 1

int changes = -1;
int nthreads = -1;
char *key = NULL;

static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;
static int start_count = 0;


typedef struct {
    pthread_t tid;
    pthread_attr_t attr;
    int n;
    flux_t h;
} thd_t;

static void signal_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        errn_exit (rc, "pthread_mutex_lock");
    start_count++;
    if ((rc = pthread_mutex_unlock (&start_lock)))
        errn_exit (rc, "pthread_mutex_unlock");
    if ((rc = pthread_cond_signal (&start_cond)))
        errn_exit (rc, "pthread_cond_signal");
}

static void wait_ready (void)
{
    int rc;

    if ((rc = pthread_mutex_lock (&start_lock)))
        errn_exit (rc, "pthread_mutex_lock");
    while (start_count < nthreads) {
        if ((rc = pthread_cond_wait (&start_cond, &start_lock)))
            errn_exit (rc, "pthread_cond_wait");
    }
    if ((rc = pthread_mutex_unlock (&start_lock)))
        errn_exit (rc, "pthread_mutex_unlock");
}

/* expect val: {-1,0,1,...,(changes - 1)}
 * count will therefore run 0...changes.
 */
static int watch_cb (const char *k, int val, void *arg, int errnum)
{
    if (errnum == 0 && val + 1 == changes)
        return -1;
    return 0;
}

void *thread (void *arg)
{
    thd_t *t = arg;

    if (!(t->h = flux_api_open ())) {
        err ("%d: cmb_init", t->n);
        goto done;
    }
    signal_ready ();
    /* The first kvs.watch reply is handled synchronously, then other kvs.watch
     * replies will arrive asynchronously and be handled by the reactor.
     */
    if (kvs_watch_int (t->h, key, watch_cb, t) < 0) {
        err ("%d: kvs_watch_int", t->n);
        goto done;
    }
    if (flux_reactor_start (t->h) < 0) {
        err ("%d: flux_reactor_start", t->n);
        goto done;
    }
done:
    if (t->h)
        flux_api_close (t->h);

    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    int i, rc;
    flux_t h;

    log_init (basename (argv[0]));

    if (argc != 4) {
        fprintf (stderr, "Usage: tkvswatch nthreads changes key\n");
        exit (1);
    }
    nthreads = strtoul (argv[1], NULL, 10);
    changes = strtoul (argv[2], NULL, 10);
    key = argv[3];

    thd = xzmalloc (sizeof (*thd) * nthreads);

    if (!(h = flux_api_open ()))
        err_exit ("cmb_init");

    if (kvs_put_int (h, key, -1) < 0)
        err_exit ("kvs_put_int %s", key);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].tid, &thd[i].attr, thread, &thd[i])))
            errn (rc, "pthread_create");
    }
    wait_ready ();

    for (i = 0; i < changes; i++) {
        if (kvs_put_int (h, key, i) < 0)
            err_exit ("kvs_put_int %s", key);
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");
    }

    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].tid, NULL)))
            errn (rc, "pthread_join");
    }

    free (thd);

    flux_api_close (h);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
