/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libkvs/kvs_lookup.h"

#define OPTIONS "Wrt"
static const struct option longopts[] = {
   {"waitcreate", no_argument, 0, 'W'},
   {"lookup_get_raw", no_argument, 0, 'r'},
   {0, 0, 0, 0},
};

enum {
    LOOKUP_GET = 1,
    LOOKUP_GET_RAW = 2,
};

static flux_future_t *fwatch;

static int func = LOOKUP_GET;

static void usage (void)
{
    fprintf (stderr,
             "Usage: watch_initial_sentinel [-W] [-r] <key>\n");
    exit (1);
}

void cancel_cb (int sig)
{
    flux_kvs_lookup_cancel (fwatch);
}

void lookup_get (flux_future_t *f)
{
    const char *value;
    if (flux_kvs_lookup_get (f, &value) < 0) {
        if (errno != ENODATA)
            log_err_exit ("flux_kvs_lookup_get");
        flux_future_destroy (f);
        return;
    }
    if (!value)
        printf ("sentinel\n");
    else
        printf ("%s\n", value);
    fflush (stdout);
    flux_future_reset (f);
}

void lookup_get_raw (flux_future_t *f)
{
    const void *data;
    size_t len;
    if (flux_kvs_lookup_get_raw (f, &data, &len) < 0) {
        if (errno != ENODATA)
            log_err_exit ("flux_kvs_lookup_get_raw");
        flux_future_destroy (f);
        return;
    }
    if (!data && !len)
        printf ("sentinel\n");
    else
        printf ("%s\n", (char *)data);
    fflush (stdout);
    flux_future_reset (f);
}

void lookup_continuation (flux_future_t *f, void *arg)
{
    switch (func) {
    case LOOKUP_GET:
        lookup_get (f);
        break;
    case LOOKUP_GET_RAW:
        lookup_get_raw (f);
        break;
    default:
        log_err_exit ("invalid func type");
    }
}

int main (int argc, char **argv)
{
    flux_t *h;
    char *key;
    int flags;
    int Wopt = 0;
    int ch;

    log_init (argv[0]);

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'W':
                Wopt = true;
                break;
            case 'r':
                func = LOOKUP_GET_RAW;
                break;
            default:
                usage ();
        }
    }

    if ((argc - optind) != 1)
        usage();

    key = argv[optind];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    flags = FLUX_KVS_WATCH;
    flags |= FLUX_KVS_WATCH_APPEND;
    flags |= FLUX_KVS_WATCH_INITIAL_SENTINEL;
    if (Wopt)
        flags |= FLUX_KVS_WAITCREATE;

    if (!(fwatch = flux_kvs_lookup (h, NULL, flags, key)))
        log_err_exit ("flux_kvs_lookup");

    if (flux_future_then (fwatch, -1., lookup_continuation, NULL) < 0)
        log_err_exit ("flux_future_then");

    if (signal (SIGUSR1, cancel_cb) == SIG_ERR)
        log_err_exit ("signal");

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
