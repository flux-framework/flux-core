/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* torture.c - kvs torture test */

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdarg.h>
#include <inttypes.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

#define OPTIONS "hc:s:p:qv"
static const struct option longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"quiet", no_argument, 0, 'q'},
    {"verbose", no_argument, 0, 'v'},
    {"count", required_argument, 0, 'c'},
    {"size", required_argument, 0, 's'},
    {"prefix", required_argument, 0, 'p'},
    {0, 0, 0, 0},
};

static void fill (char *s, int i, int len);

void usage (void)
{
    fprintf (stderr,
             "Usage: torture [--quiet|--verbose] [--prefix NAME] [--size "
             "BYTES] "
             "[--count N]\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    int i, count = 20;
    int size = 20;
    char *key, *val;
    bool quiet = false;
    struct timespec t0;
    char *prefix = NULL;
    bool verbose = false;
    const char *s;
    flux_future_t *f;
    flux_kvs_txn_t *txn;

    log_init ("torture");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
        case 'h': /* --help */
            usage ();
            break;
        case 's': /* --size BYTES */
            size = strtoul (optarg, NULL, 10);
            break;
        case 'c': /* --count */
            count = strtoul (optarg, NULL, 10);
            break;
        case 'p': /* --prefix NAME */
            prefix = xstrdup (optarg);
            break;
        case 'v': /* --verbose */
            verbose = true;
            break;
        case 'q': /* --quiet */
            quiet = true;
            break;
        default:
            usage ();
            break;
        }
    }
    if (optind != argc)
        usage ();
    if (size < 1 || count < 1)
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!prefix) {
        uint32_t rank;
        if (flux_get_rank (h, &rank) < 0)
            log_err_exit ("flux_get_rank");
        prefix = xasprintf ("kvstorture-%" PRIu32, rank);
    }
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_unlink (txn, 0, prefix) < 0)
        log_err_exit ("flux_kvs_txn_unlink");
    if (!(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);

    val = xzmalloc (size);

    monotime (&t0);
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        if (flux_kvs_txn_pack (txn, 0, key, "s", val) < 0)
            log_err_exit ("flux_kvs_txn_pack");
        if (verbose)
            log_msg ("%s = %s", key, val);
        free (key);
    }
    if (!quiet)
        log_msg ("kvs_put:    time=%0.3f s (%d keys of size %d)",
                 monotime_since (t0) / 1000,
                 count,
                 size);

    monotime (&t0);
    if (!(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    if (!quiet)
        log_msg ("kvs_commit: time=%0.3f s", monotime_since (t0) / 1000);

    monotime (&t0);
    for (i = 0; i < count; i++) {
        if (asprintf (&key, "%s.key%d", prefix, i) < 0)
            oom ();
        fill (val, i, size);
        if (!(f = flux_kvs_lookup (h, NULL, 0, key))
            || flux_kvs_lookup_get_unpack (f, "s", &s) < 0)
            log_err_exit ("flux_kvs_lookup '%s'", key);
        if (verbose)
            log_msg ("%s = %s", key, s);
        if (strcmp (s, val) != 0)
            log_msg_exit ("kvs_lookup: key '%s' wrong value '%s'", key, s);
        free (key);
        flux_future_destroy (f);
    }
    if (!quiet)
        log_msg ("kvs_lookup:    time=%0.3f s (%d keys of size %d)",
                 monotime_since (t0) / 1000,
                 count,
                 size);

    if (prefix)
        free (prefix);
    flux_close (h);
    log_fini ();
    return 0;
}

static void fill (char *s, int i, int len)
{
    snprintf (s, len, "%d", i);
    memset (s + strlen (s), 'x', len - strlen (s) - 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
