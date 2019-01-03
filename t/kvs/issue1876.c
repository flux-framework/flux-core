/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    const char *key;
    int i;

    if (argc != 2) {
        fprintf (stderr, "Usage: watch_cancel_loop key\n");
        return (1);
    }
    key = argv[1];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    for (i = 0; i < 1000; i++) {
        flux_future_t *f;

        log_msg ("loop=%d", i);

        if (!(f = flux_kvs_lookup (h, FLUX_KVS_WATCH
                                   | FLUX_KVS_WAITCREATE, key)))
            log_err_exit ("flux_kvs_lookup");
        if (flux_kvs_lookup_cancel (f) < 0)
            log_err_exit ("flux_kvs_lookup_cancel");
        /* Consume responses until ENODATA from cancel appears
         */
        while (flux_kvs_lookup_get (f, NULL) == 0)
            ;
        if (errno != ENODATA)
            log_err_exit ("flux_kvs_lookup_get");
        flux_future_destroy (f);
    }

    flux_close (h);
    return (0);
}

