/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

static void usage (void)
{
    fprintf (stderr, "Usage: watch_stream <key>\n");
    exit (1);
}

void stream_continuation (flux_future_t *f, void *arg)
{
    const char *value;
    int *replycount = arg;
    if (flux_kvs_lookup_get (f, &value) < 0) {
        if (errno != ENODATA)
            log_err_exit ("flux_kvs_lookup_get");
        flux_future_destroy (f);
        return;
    }
    printf ("%d: %s\n", ++(*replycount), value);
    flux_future_reset (f);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *key;
    int replycount = 0;

    if (argc != 2)
        usage();

    key = argv[1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_STREAM, key)))
        log_err_exit ("flux_kvs_lookup");

    if (flux_future_then (f, -1., stream_continuation, &replycount) < 0)
        log_err_exit ("flux_future_then");

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
