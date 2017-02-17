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

/* commitmerge test
 *
 * Basic purpose of this test is to test if commit merging does not
 * work properly when commit-merge is disabled in the kvs
 * (commit-merge=0).
 *
 * A watch thread will watch a key.
 * A number of commit threads will commit a value to the key.
 *
 * The watch should see every single change if commit merging is not enabled.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>
#include <inttypes.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    pthread_t t;
    pthread_attr_t attr;
    int n;
    flux_t *h;
} thd_t;

typedef struct {
    thd_t *t;
    int lastcount;
} watch_count_t;

#define KEYSUFFIX "commitwatch-key"

#define WATCH_TIMEOUT 5

static int threadcount = -1;
static int changecount = 0;
static char *prefix = NULL;
static char *key = NULL;
static bool nopt = false;

static int watch_init = 0;
static pthread_cond_t watch_init_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t watch_init_lock = PTHREAD_MUTEX_INITIALIZER;

#define OPTIONS "n"
static const struct option longopts[] = {
    {"nomerge", no_argument, 0, 'n'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: commitmerge [--nomerge] threadcount prefix\n");
    exit (1);
}

void watch_prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    /* Tell main it can now launch commit threads */
    if (!watch_init) {
        pthread_mutex_lock (&watch_init_lock);
        watch_init++;
        pthread_cond_signal (&watch_init_cond);
        pthread_mutex_unlock (&watch_init_lock);
    }

    if (changecount >= threadcount)
        flux_reactor_stop (r);
}

static int watch_count_cb (const char *key, int val, void *arg, int errnum)
{
    thd_t *t = arg;

    /* First value should be empty & get ENOENT */
    if (!errnum) {
        //printf ("watch %s = %d\n", key, val);
        changecount++;
    }

    if (changecount == threadcount)
        kvs_unwatch (t->h, key);
    return 0;
}

static void watch_timeout_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    watch_count_t *wc = arg;

    /* timeout */
    if (wc->lastcount == changecount)
        flux_reactor_stop (r);
    else
        wc->lastcount = changecount;
}

void *watchthread (void *arg)
{
    thd_t *t = arg;
    watch_count_t wc;
    flux_reactor_t *r;
    flux_watcher_t *pw = NULL;
    flux_watcher_t *tw = NULL;
    char *json_str = NULL;
    int rc;

    if (!(t->h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* Make sure key doesn't already exist, initial value may affect
     * test by chance (i.e. initial value = 0, commit 0 and thus no
     * change)
     */

    rc = kvs_get (t->h, key, &json_str);
    if (rc < 0 && errno != ENOENT)
        log_err_exit ("kvs_get");

    if (!rc) {
        if (kvs_unlink (t->h, key) < 0)
            log_err_exit ("kvs_unlink");
        if (kvs_commit (t->h, 0) < 0)
            log_err_exit ("kvs_commit");
    }

    r = flux_get_reactor (t->h);

    if (kvs_watch_int (t->h, key, watch_count_cb, t) < 0)
        log_err_exit ("kvs_watch_int %s", key);

    pw = flux_prepare_watcher_create (r, watch_prepare_cb, NULL);

    wc.t = t;
    wc.lastcount = -1;

    /* So test won't hang if there's a bug */
    tw = flux_timer_watcher_create (r,
                                    WATCH_TIMEOUT,
                                    WATCH_TIMEOUT,
                                    watch_timeout_cb,
                                    &wc);

    flux_watcher_start (pw);
    flux_watcher_start (tw);

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (json_str)
        free (json_str);
    flux_watcher_destroy (pw);
    flux_watcher_destroy (tw);
    flux_close (t->h);
    return NULL;
}

void *committhread (void *arg)
{
    thd_t *t = arg;

    if (!(t->h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (kvs_put_int (t->h, key, t->n) < 0)
        log_err_exit ("%s", key);

    if (kvs_commit (t->h, nopt ? KVS_NO_MERGE : 0) < 0)
        log_err_exit ("kvs_commit");

    flux_close (t->h);
    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    int i, rc, ch;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'n':
            nopt = true;
            break;
        default:
            usage ();
        }
    }

    if (argc - optind != 2)
        usage ();

    threadcount = strtoul (argv[optind++], NULL, 10);
    if (!threadcount)
        log_msg_exit ("thread count must be > 0");
    prefix = argv[optind++];

    key = xasprintf ("%s.%s", prefix, KEYSUFFIX);

    /* +1 for watch thread */
    thd = xzmalloc (sizeof (*thd) * (threadcount + 1));

    /* start watch thread */
    thd[threadcount].n = threadcount;
    if ((rc = pthread_attr_init (&thd[threadcount].attr)))
        log_errn (rc, "pthread_attr_init");
    if ((rc = pthread_create (&thd[threadcount].t,
                              &thd[threadcount].attr,
                              watchthread,
                              &thd[threadcount])))
        log_errn (rc, "pthread_create");

    /* Wait for watch thread to setup */
    pthread_mutex_lock (&watch_init_lock);
    while (!watch_init)
        pthread_cond_wait (&watch_init_cond, &watch_init_lock);
    pthread_mutex_unlock (&watch_init_lock);

    /* start commit threads */
    for (i = 0; i < threadcount; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, committhread, &thd[i])))
            log_errn (rc, "pthread_create");
    }

    /* +1 for watch thread */
    for (i = 0; i < (threadcount + 1); i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            log_errn (rc, "pthread_join");
    }

    printf("%d\n", changecount);

    free (thd);
    free (key);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
