/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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

#define KEYSUFFIX "transactionmerge-key"

#define WATCH_TIMEOUT 5.0

#define MAX_ITERS 50

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

static void watch_count_cb (flux_future_t *f, void *arg)
{
    if (flux_rpc_get (f, NULL)) {
        if (errno == ETIMEDOUT)
            log_msg_exit ("timeout: saw %d changes", changecount);
        if (errno != ENODATA)
            log_err_exit ("flux_rpc_get");
        flux_future_destroy (f);
        return;
    }

    changecount++;

    flux_future_reset (f);

    /* re-call to set timeout */
    if (flux_future_then (f, WATCH_TIMEOUT, watch_count_cb, arg) < 0)
        log_err_exit ("flux_future_then");

    if (changecount == threadcount) {
        if (flux_kvs_lookup_cancel (f) < 0)
            log_err_exit ("flux_kvs_lookup_cancel");
    }
}

static void watch_init_cb (flux_future_t *f, void *arg)
{
    /* Tell main it can now launch commit threads */
    if (!watch_init) {
        pthread_mutex_lock (&watch_init_lock);
        watch_init++;
        pthread_cond_signal (&watch_init_cond);
        pthread_mutex_unlock (&watch_init_lock);
    }

    flux_future_reset (f);

    /* set alternate cb for counting */
    if (flux_future_then (f, WATCH_TIMEOUT, watch_count_cb, arg) < 0)
        log_err_exit ("flux_future_then");
}

void *watchthread (void *arg)
{
    thd_t *t = arg;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    flux_reactor_t *r;

    if (!(t->h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* set an initial value to sync w/ watch_init */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, 0, key, "init-val") < 0)
        log_err_exit ("flux_kvs_txn_put");
    if (!(f = flux_kvs_commit (t->h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    r = flux_get_reactor (t->h);

    if (!(f = flux_kvs_lookup (t->h,
                               NULL,
                               FLUX_KVS_WATCH,
                               key)))
        log_err_exit ("flux_kvs_lookup %s", key);

    if (flux_future_then (f, WATCH_TIMEOUT, watch_init_cb, t) < 0)
        log_err_exit ("flux_future_then %s", key);

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (t->h);
    return NULL;
}

void *committhread (void *arg)
{
    thd_t *t = arg;
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (!(t->h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, key, "i", t->n) < 0)
        log_err_exit ("%s", key);
    if (!(f = flux_kvs_commit (t->h, NULL, nopt ? FLUX_KVS_NO_MERGE : 0, txn))
            || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");

    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
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
