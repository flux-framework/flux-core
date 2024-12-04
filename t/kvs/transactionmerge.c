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

    if (changecount == threadcount) {
        if (flux_kvs_lookup_cancel (f) < 0)
            log_err_exit ("flux_kvs_lookup_cancel");
    }
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
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    flux_t *h;
    flux_reactor_t *r;
    int i, rc, ch;

    log_init (argv[0]);

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

    /*
     * first setup watcher
     */

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* set an initial value */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_put (txn, 0, key, "init-val") < 0)
        log_err_exit ("flux_kvs_txn_put");
    if (!(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    r = flux_get_reactor (h);

    if (!(f = flux_kvs_lookup (h,
                               NULL,
                               FLUX_KVS_WATCH,
                               key)))
        log_err_exit ("flux_kvs_lookup %s", key);

    /* wait for initial response */
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");

    flux_future_reset (f);

    /* set cb for counting */
    if (flux_future_then (f, WATCH_TIMEOUT, watch_count_cb, NULL) < 0)
        log_err_exit ("flux_future_then");

    thd = xzmalloc (sizeof (*thd) * threadcount);

    /*
     * start commit threads
     */

    for (i = 0; i < threadcount; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, committhread, &thd[i])))
            log_errn (rc, "pthread_create");
    }

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    for (i = 0; i < threadcount; i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            log_errn (rc, "pthread_join");
    }

    printf("%d\n", changecount);

    free (thd);
    free (key);

    flux_close (h);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
