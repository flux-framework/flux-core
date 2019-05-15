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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"

typedef struct {
    pthread_t t;
    pthread_attr_t attr;
    int n;
    flux_t *h;
    zlist_t *perf;
} thd_t;

static int count = -1;
static int nthreads = -1;
static char *prefix = NULL;
static bool fopt = false;
static bool sopt = false;
static bool nopt = false;
static int nopt_divisor = 1;
static int fence_nprocs;

#define OPTIONS "f:sn:"
static const struct option longopts[] = {
    {"fence", required_argument, 0, 'f'},
    {"stats", no_argument, 0, 's'},
    {"nomerge", required_argument, 0, 'n'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
             "Usage: commit [--fence N] [--stats] [--nomerge N] nthreads count "
             "prefix\n");
    exit (1);
}

double *ddup (double d)
{
    double *dcpy = xzmalloc (sizeof (double));
    *dcpy = d;
    return dcpy;
}

void *thread (void *arg)
{
    thd_t *t = arg;
    char *key, *fence_name = NULL;
    int i, flags = 0;
    struct timespec t0;
    uint32_t rank;
    flux_future_t *f;
    flux_kvs_txn_t *txn;

    if (!(t->h = flux_open (NULL, 0))) {
        log_err ("%d: flux_open", t->n);
        goto done;
    }
    if (flux_get_rank (t->h, &rank) < 0) {
        log_err ("%d: flux_get_rank", t->n);
        goto done;
    }
    for (i = 0; i < count; i++) {
        if (!(txn = flux_kvs_txn_create ()))
            log_err_exit ("flux_kvs_txn_create");
        key = xasprintf ("%s.%" PRIu32 ".%d.%d", prefix, rank, t->n, i);
        if (fopt)
            fence_name = xasprintf ("%s-%d", prefix, i);
        if (sopt)
            monotime (&t0);
        if (flux_kvs_txn_pack (txn, 0, key, "i", 42) < 0)
            log_err_exit ("%s", key);
        if (nopt && (i % nopt_divisor) == 0)
            flags |= FLUX_KVS_NO_MERGE;
        else
            flags = 0;
        if (fopt) {
            if (!(f = flux_kvs_fence (t->h, NULL, flags, fence_name, fence_nprocs, txn))
                || flux_future_get (f, NULL) < 0)
                log_err_exit ("flux_kvs_fence");
            flux_future_destroy (f);
        } else {
            if (!(f = flux_kvs_commit (t->h, NULL, flags, txn))
                || flux_future_get (f, NULL) < 0)
                log_err_exit ("flux_kvs_commit");
            flux_future_destroy (f);
        }
        if (sopt && zlist_append (t->perf, ddup (monotime_since (t0))) < 0)
            oom ();
        free (key);
        free (fence_name);
        flux_kvs_txn_destroy (txn);
    }
done:
    if (t->h)
        flux_close (t->h);
    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    int i, rc;
    int ch;
    tstat_t ts;
    struct timespec t0;

    log_init (basename (argv[0]));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'f':
                fopt = true;
                fence_nprocs = strtoul (optarg, NULL, 10);
                if (!fence_nprocs)
                    log_msg_exit ("fence value must be > 0");
                break;
            case 's':
                sopt = true;
                break;
            case 'n':
                nopt = true;
                nopt_divisor = strtoul (optarg, NULL, 10);
                if (!nopt_divisor)
                    log_msg_exit ("nopt value must be > 0");
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 3)
        usage ();

    nthreads = strtoul (argv[optind++], NULL, 10);
    if (!nthreads)
        log_msg_exit ("thread count must be > 0");
    count = strtoul (argv[optind++], NULL, 10);
    if (!count)
        log_msg_exit ("commit count must be > 0");
    prefix = argv[optind++];

    memset (&ts, 0, sizeof (ts));

    thd = xzmalloc (sizeof (*thd) * nthreads);

    if (sopt)
        monotime (&t0);

    for (i = 0; i < nthreads; i++) {
        thd[i].n = i;
        if (!(thd[i].perf = zlist_new ()))
            oom ();
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, thread, &thd[i])))
            log_errn (rc, "pthread_create");
    }

    for (i = 0; i < nthreads; i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            log_errn (rc, "pthread_join");
        if (sopt) {
            double *e;
            while ((e = zlist_pop (thd[i].perf))) {
                tstat_push (&ts, *e);
                free (e);
            }
        }
        zlist_destroy (&thd[i].perf);
    }

    if (sopt) {
        json_t *o;
        char *s;

        if (!(o = json_pack ("{s:{s:i s:f s:f s:f s:f} s:f}",
                             "put+commit times (sec)",
                             "count",
                             tstat_count (&ts),
                             "min",
                             tstat_min (&ts) * 1E-3,
                             "mean",
                             tstat_mean (&ts) * 1E-3,
                             "stddev",
                             tstat_stddev (&ts) * 1E-3,
                             "max",
                             tstat_max (&ts) * 1E-3,
                             "put+commit throughput (#/sec)",
                             (double)(count * nthreads)
                                 / (monotime_since (t0) * 1E-3))))
            log_err_exit ("json_pack");
        if (!(s = json_dumps (o, JSON_INDENT (2))))
            log_err_exit ("json_dumps");
        printf ("%s\n", s);
        json_decref (o);
        free (s);
    }

    free (thd);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
