/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
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

static void usage (void)
{
    fprintf (stderr, "Usage: lookup_invalid key\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    char *key = NULL;
    flux_future_t *f = NULL;

    log_init (basename (argv[0]));

    if (argc != 2)
        usage ();
    key = argv[1];

    if (!(h = flux_open (NULL, 0))) {
        log_err_exit ("flux_open");
        goto done;
    }

    /* invalid lookup - do not specify namespace or root ref */
    if (!(f = flux_rpc_pack (h,
                             "kvs.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i}",
                             "key",
                             key,
                             "flags",
                             0)))
        log_err_exit ("flux_rpc_pack");

    if (flux_future_get (f, NULL) < 0) {
        printf ("flux_future_get: %s\n", flux_strerror (errno));
        goto done;
    }

done:
    flux_future_destroy (f);
    flux_close (h);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
