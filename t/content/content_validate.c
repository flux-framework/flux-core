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
#include <flux/core.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/log.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    uint32_t hash[BLOBREF_MAX_DIGEST_SIZE];
    ssize_t hash_size;
    const char *ref;
    flux_future_t *f;

    if (argc != 2) {
        fprintf (stderr, "Usage: content_validate <ref>\n");
        return (1);
    }
    ref = argv[1];

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if ((hash_size = blobref_strtohash (ref, hash, sizeof (hash))) < 0)
        log_err_exit ("blobref_strtohash");

    if (!(f = flux_rpc_raw (h,
                            "content-backing.validate",
                            hash,
                            hash_size,
                            0,
                            0)))
        log_err_exit ("flux_rpc_raw");

    if (flux_rpc_get (f, NULL) < 0)
        log_err_exit ("flux_rpc_get");
    printf ("valid\n");
    flux_close (h);
    return (0);
}
