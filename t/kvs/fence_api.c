/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* commit - performance test for KVS commits */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"
#include "ccan/str/str.h"

typedef struct {
    pthread_t t;
    pthread_attr_t attr;
    int n;
    flux_t *h;
    char *treeobj;
    char *rootref;
    int sequence;
} thd_t;

static int count = -1;
static char *prefix = NULL;
static char *fence_name;
static bool syncflag = false;
static bool symlinkflag = false;
static const char *namespace = NULL;

#define OPTIONS "n:Ss"
static const struct option longopts[] = {
    {"namespace", required_argument, 0, 'n'},
    {"sync",      no_argument,       0, 'S'},
    {"symlink",   no_argument,       0, 's'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
             "Usage: fence_api "
             "[--sync] [--symlink] [--namespace=ns] "
             "count prefix\n");
    exit (1);
}

void *thread (void *arg)
{
    thd_t *t = arg;
    char *key;
    uint32_t rank;
    flux_future_t *f;
    flux_kvs_txn_t *txn;
    const char *treeobj;
    const char *rootref;
    int sequence;
    int flags = 0;

    if (!(t->h = flux_open (NULL, 0))) {
        log_err ("%d: flux_open", t->n);
        goto done;
    }

    if (flux_get_rank (t->h, &rank) < 0) {
        log_err ("%d: flux_get_rank", t->n);
        goto done;
    }

    /* create some key and write something to it */

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");

    key = xasprintf ("%s.%"PRIu32".%d", prefix, rank, t->n);

    if (symlinkflag) {
        if (flux_kvs_txn_symlink (txn, 0, key, NULL, "a-target") < 0)
            log_err_exit ("%s", key);
    }
    else {
        if (flux_kvs_txn_pack (txn, 0, key, "i", 42) < 0)
            log_err_exit ("%s", key);
    }

    if (syncflag)
        flags |= FLUX_KVS_SYNC;

    if (!(f = flux_kvs_fence (t->h, namespace, flags, fence_name,
                              count, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_fence");

    /* save off fence root information */

    if (flux_kvs_commit_get_treeobj (f, &treeobj) < 0)
        log_err_exit ("flux_kvs_commit_get_treeobj");
    if (flux_kvs_commit_get_rootref (f, &rootref) < 0)
        log_err_exit ("flux_kvs_commit_get_rootref");
    if (flux_kvs_commit_get_sequence (f, &sequence) < 0)
        log_err_exit ("flux_kvs_commit_get_sequence");

    t->treeobj = xstrdup (treeobj);
    t->rootref = xstrdup (rootref);
    t->sequence = sequence;

    flux_future_destroy (f);

    free (key);
    flux_kvs_txn_destroy (txn);

done:
    if (t->h)
        flux_close (t->h);
    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    int i, ch, num, rc;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'S':
            syncflag = true;
            break;
        case 's':
            symlinkflag = true;
            break;
        case 'n':
            namespace = optarg;
            break;
        default:
            usage ();
        }
    }
    if ((argc - optind) != 2)
        usage ();

    count = strtoul (argv[optind], NULL, 10);
    if (count <= 1)
        log_msg_exit ("commit count must be > 1");
    prefix = argv[optind+1];

    /* create a fence name for this test that is random-ish */
    srand (time (NULL));
    num = rand ();
    fence_name = xasprintf ("%s-%d", prefix, num);

    thd = xzmalloc (sizeof (*thd) * count);

    for (i = 0; i < count; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, thread, &thd[i])))
            log_errn (rc, "pthread_create");
    }

    for (i = 0; i < count; i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            log_errn (rc, "pthread_join");
    }

    /* compare results from all of the fences, the root ref info
     * should all be the same
     */
    for (i = 1; i < count; i++) {
        if (!streq (thd[0].treeobj, thd[i].treeobj))
            log_msg_exit ("treeobj mismatch: %s != %s\n",
                          thd[0].treeobj, thd[i].treeobj);
        if (!streq (thd[0].rootref, thd[i].rootref))
            log_msg_exit ("rootref mismatch: %s != %s\n",
                          thd[0].rootref, thd[i].rootref);
        if (thd[0].sequence != thd[i].sequence)
            log_msg_exit ("sequence mismatch: %d != %d\n",
                          thd[0].sequence, thd[i].sequence);
    }

    for (i = 0; i < count; i++) {
        free (thd[i].treeobj);
        free (thd[i].rootref);
    }
    free (thd);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
