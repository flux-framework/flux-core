/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* commit_order.c - ensure watch responses are returned in commit order */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

static bool verbose;
static int totcount = 1000;
static int max_queue_depth = 16;
static char *ns = NULL;
static const char *key;

static int txcount;   // count of commit requests
static int rxcount;   // count of commit responses
static int wrxcount;  // count of lookup/watch responses
static flux_watcher_t *w_prep;
static flux_watcher_t *w_check;
static flux_watcher_t *w_idle;

void watch_continuation (flux_future_t *f, void *arg);
void commit_continuation (flux_future_t *f, void *arg);
flux_future_t *commit_int (flux_t *h, const char *k, int v);
void prep (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
void check (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);

#define OPTIONS "hvc:f:n:"
static const struct option longopts[] = {
    {"help", no_argument, 0, 'h'},
    {"verbose", no_argument, 0, 'v'},
    {"count", required_argument, 0, 'c'},
    {"fanout", required_argument, 0, 'f'},
    {"namespace", required_argument, 0, 'n'},
    {0, 0, 0, 0},
};

void usage (void)
{
    fprintf (stderr,
             "Usage: commit_order [--verbose] [--namespace=NAME] [--count=N] "
             "[--fanout=N] key\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *r;
    int last = -1;
    int ch;
    flux_future_t *f;

    log_init ("commit_order");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'v': /* --verbose */
                verbose = true;
                break;
            case 'c': /* --count N */
                totcount = strtoul (optarg, NULL, 10);
                break;
            case 'f': /* --fanout N */
                max_queue_depth = strtoul (optarg, NULL, 10);
                break;
            case 'n': /* --namespace=NAME */
                if (!(ns = strdup (optarg)))
                    log_err_exit ("out of memory");
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    key = argv[optind++];
    if (totcount < 1 || max_queue_depth < 1)
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(r = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");
    /* One synchronous put before watch request, so that
     * watch request doesn't fail with ENOENT.
     */
    f = commit_int (h, key, txcount++);
    commit_continuation (f, NULL);  // destroys f, increments rxcount

    /* Configure watcher
     * Wait for one response before unleashing async puts, to ensure
     * that first value is captured.
     */
    if (!(f = flux_kvs_lookup (h, ns, FLUX_KVS_WATCH, key)))
        log_err_exit ("flux_kvs_lookup");
    watch_continuation (f, &last);  // resets f, increments wrxcount
    if (flux_future_then (f, -1., watch_continuation, &last) < 0)
        log_err_exit ("flux_future_then");

    /* Configure mechanism to keep max_queue_depth (--fanout) put RPCs
     * outstanding until totcount (--count) reached.
     */
    if (!(w_prep = flux_prepare_watcher_create (r, prep, NULL)))
        log_err_exit ("flux_prepare_watcher_create");
    if (!(w_check = flux_check_watcher_create (r, check, h)))
        log_err_exit ("flux_check_watcher_create");
    if (!(w_idle = flux_idle_watcher_create (r, NULL, NULL)))
        log_err_exit ("flux_idle_watcher_create");
    flux_watcher_start (w_prep);
    flux_watcher_start (w_check);

    /* Run until work is exhausted.
     */
    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_watcher_destroy (w_prep);
    flux_watcher_destroy (w_check);
    flux_watcher_destroy (w_idle);

    free (ns);
    flux_close (h);
    log_fini ();
    return 0;
}

void watch_continuation (flux_future_t *f, void *arg)
{
    int *last = arg;
    int i;

    if (flux_kvs_lookup_get_unpack (f, "i", &i) < 0) {
        if (errno == ENODATA) {
            flux_future_destroy (f);  // ENODATA (like EOF on response stream)
            if (verbose)
                printf ("< ENODATA\n");
        } else
            log_err_exit ("flux_lookup_get_unpack");
        return;
    }
    if (verbose)
        printf ("< %s=%d\n", key, i);
    if (i != *last + 1)
        log_msg_exit ("%s: got %d, expected %d", __FUNCTION__, i, *last + 1);
    if (++wrxcount == totcount)
        flux_kvs_lookup_cancel (f);
    *last = i;

    flux_future_reset (f);
}

void commit_continuation (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_kvs_commit");
    rxcount++;

    flux_future_destroy (f);
}

flux_future_t *commit_int (flux_t *h, const char *k, int v)
{
    flux_kvs_txn_t *txn;
    flux_future_t *f;

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_pack (txn, 0, k, "i", v) < 0)
        log_err_exit ("flux_kvs_txn_pack");
    if (!(f = flux_kvs_commit (h, ns, FLUX_KVS_NO_MERGE, txn)))
        log_err_exit ("flux_kvs_commit");
    flux_kvs_txn_destroy (txn);
    if (verbose)
        printf ("> %s=%d\n", k, v);
    return f;
}

void prep (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    if (txcount == totcount) {
        flux_watcher_stop (w_prep);
        flux_watcher_stop (w_check);
    } else if ((txcount - rxcount) < max_queue_depth)
        flux_watcher_start (w_idle);  // keeps loop from blocking
}

void check (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    flux_t *h = arg;

    flux_watcher_stop (w_idle);

    if (txcount < totcount && (txcount - rxcount) < max_queue_depth) {
        flux_future_t *f;

        f = commit_int (h, key, txcount++);
        if (flux_future_then (f, -1.0, commit_continuation, NULL) < 0)
            log_err_exit ("flux_future_then");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
