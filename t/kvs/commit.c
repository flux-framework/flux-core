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

/* tcommit - performance test for KVS commits */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/shortjson.h"
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
static int fence_nprocs;

#define OPTIONS "f:s"
static const struct option longopts[] = {
   {"fence",   required_argument,   0, 'f'},
   {"stats",   no_argument,         0, 's'},
   {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, "Usage: tcommit [--fence N] [--stats] nthreads count prefix\n");
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
    char *key, *fence = NULL;
    int i;
    struct timespec t0;
    uint32_t rank;

    if (!(t->h = flux_open (NULL, 0))) {
        log_err ("%d: flux_open", t->n);
        goto done;
    }
    if (flux_get_rank (t->h, &rank) < 0) {
        log_err ("%d: flux_get_rank", t->n);
        goto done;
    }
    for (i = 0; i < count; i++) {
        key = xasprintf ("%s.%u.%d.%d", prefix, rank, t->n, i);
        if (fopt)
            fence = xasprintf ("%s-%d", prefix, i);
        if (sopt)
            monotime (&t0);
        if (kvs_put_int (t->h, key, 42) < 0)
            log_err_exit ("%s", key);
        if (fopt) {
            if (kvs_fence (t->h, fence, fence_nprocs) < 0)
                log_err_exit ("kvs_fence");
        } else {
            if (kvs_commit (t->h) < 0)
                log_err_exit ("kvs_commit");
        }
        if (sopt && zlist_append (t->perf, ddup (monotime_since (t0))) < 0)
            oom ();
        free (key);
        if (fence)
            free (fence);
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
                break;
            case 's':
                sopt = true;
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 3)
        usage ();

    nthreads = strtoul (argv[optind++], NULL, 10);
    count = strtoul (argv[optind++], NULL, 10);
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
        json_object *o = Jnew ();
        json_object *t = Jnew ();

        Jadd_int (t, "count", tstat_count (&ts));
        Jadd_double (t, "min", tstat_min (&ts)*1E-3);
        Jadd_double (t, "mean", tstat_mean (&ts)*1E-3);
        Jadd_double (t, "stddev", tstat_stddev (&ts)*1E-3);
        Jadd_double (t, "max", tstat_max (&ts)*1E-3);

        json_object_object_add (o, "put+commit times (sec)", t);
        Jadd_double (o, "put+commmit throughput (#/sec)",
                          (double)(count*nthreads)/(monotime_since (t0)*1E-3));
        printf ("%s\n",
                json_object_to_json_string_ext (o, JSON_C_TO_STRING_PRETTY));
        json_object_put (o);
    }

    free (thd);

    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
