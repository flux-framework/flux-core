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
    flux_future_t *f;

    if (argc != 2) {
        fprintf (stderr, "Usage: waitcreate_cancel key\n");
        return (1);
    }
    key = argv[1];
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_WAITCREATE, key)))
        log_err_exit ("flux_kvs_lookup");

    if (flux_kvs_lookup_cancel (f) < 0)
        log_err_exit ("flux_kvs_lookup_cancel");

    if (flux_kvs_lookup_get (f, NULL) < 0) {
        if (errno != ENODATA)
            log_err_exit ("flux_kvs_lookup_get");
        flux_future_destroy (f);
    } else
        log_msg_exit ("flux_kvs_lookup_get returned success");

    flux_close (h);
    return (0);
}
