/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* loop_append - infinite appends for testing kvs checkpointing.
 *
 * - count - stop after count appends
 * - batch-count - how many eventlog entries to append per commit
 * - threads - each thread writes to a different key
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <getopt.h>
#include <pthread.h>
#include <inttypes.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libeventlog/eventlog.h"

#define OPTIONS "c:b:t:"
static const struct option longopts[] = {
   {"count", required_argument, 0, 'c'},
   {"batch-count", required_argument, 0, 'b'},
   {"threads", required_argument, 0, 't'},
   {0, 0, 0, 0},
};

static int count = -1;
static int threads = 1;
static int batch_count = 10;
static const char *key = NULL;

static void usage (void)
{
    fprintf (stderr,
"Usage: loop_append [--count=N] [--batch-count=N] [--threads=N] <key>\n"
);
    exit (1);
}

typedef struct {
    pthread_t t;
    pthread_attr_t attr;
    int n;
    char *key;
} thd_t;

void *thread (void *arg)
{
    thd_t *t = arg;
    flux_t *h = NULL;
    char eventname[64];
    int i = 0;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    snprintf (eventname, sizeof (eventname), "test%d", t->n);

    while (count < 0 || (i < count)) {
        flux_future_t *f;
        flux_kvs_txn_t *txn;
        json_t *o;
        char *str;
        int j = 0;
        if (!(txn = flux_kvs_txn_create ()))
            log_err_exit ("flux_kvs_txn_create");
        for (j = 0; j < batch_count && (count < 0 || i < count); j++) {
            if (!(o = eventlog_entry_pack (i, eventname, "{s:i}", "count", i)))
                log_err_exit ("eventlog_entry_pack");
            if (!(str = eventlog_entry_encode (o)))
                log_err_exit ("eventlog_encode");
            if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, t->key, str) < 0)
                log_err_exit ("%s", key);
            json_decref (o);
            free (str);
            i++;
        }
        if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
            log_err_exit ("flux_kvs_commit");
        if (flux_future_get (f, NULL) < 0)
            log_err_exit ("commit %d", i);
        flux_future_destroy (f);
        flux_kvs_txn_destroy (txn);
    }
    flux_close (h);
    return NULL;
}

int main (int argc, char *argv[])
{
    thd_t *thd;
    flux_t *h = NULL;
    flux_future_t *f;
    flux_kvs_txn_t *txn;
    int i, ch, rc;

    log_init (argv[0]);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':
                count = strtoul (optarg, NULL, 10);
                if (!count)
                    log_msg_exit ("count must be > 0");
                break;
            case 'b':
                batch_count = strtoul (optarg, NULL, 10);
                if (!batch_count)
                    log_msg_exit ("batch count must be > 0");
                break;
            case 't':
                threads = strtoul (optarg, NULL, 10);
                if (!threads)
                    log_msg_exit ("threads must be > 0");
                break;
            default:
                usage ();
        }
    }
    if ((argc - optind) != 1)
        usage ();
    key = argv[optind];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* first unlink old one if it's there */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_unlink (txn, 0, key) < 0)
        log_err_exit ("flux_kvs_txn_unlink");
    if (!(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0) {
        if (errno != ENOENT)
            log_err_exit ("flux_kvs_commit");
    }

    thd = xzmalloc (sizeof (*thd) * threads);

    for (i = 0; i < threads; i++) {
        thd[i].n = i;
        if ((rc = pthread_attr_init (&thd[i].attr)))
            log_errn (rc, "pthread_attr_init");
        if (asprintf ((&thd[i].key), "%s%d", key, i) < 0)
            log_errn (rc, "asprintf");
        if ((rc = pthread_create (&thd[i].t, &thd[i].attr, thread, &thd[i])))
            log_errn (rc, "pthread_create");
    }

    for (i = 0; i < threads; i++) {
        if ((rc = pthread_join (thd[i].t, NULL)))
            log_errn (rc, "pthread_join");
    }

    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    for (i = 0; i < threads; i++)
        free (thd[i].key);
    free (thd);
    flux_close (h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
