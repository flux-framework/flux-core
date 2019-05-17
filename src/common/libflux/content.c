/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <errno.h>
#include <flux/core.h>

#include "content.h"

#include "src/common/libutil/blobref.h"

flux_future_t *flux_content_load (flux_t *h, const char *blobref, int flags)
{
    const char *topic = "content.load";
    uint32_t rank = FLUX_NODEID_ANY;

    if (!h || !blobref || blobref_validate (blobref) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & CONTENT_FLAG_UPSTREAM))
        rank = FLUX_NODEID_UPSTREAM;
    if ((flags & CONTENT_FLAG_CACHE_BYPASS)) {
        topic = "content-backing.load";
        rank = 0;
    }
    return flux_rpc_raw (h, topic, blobref, strlen (blobref) + 1, rank, 0);
}

int flux_content_load_get (flux_future_t *f, const void **buf, int *len)
{
    return flux_rpc_get_raw (f, buf, len);
}

flux_future_t *flux_content_store (flux_t *h,
                                   const void *buf,
                                   int len,
                                   int flags)
{
    const char *topic = "content.store";
    uint32_t rank = FLUX_NODEID_ANY;

    if ((flags & CONTENT_FLAG_UPSTREAM))
        rank = FLUX_NODEID_UPSTREAM;
    if ((flags & CONTENT_FLAG_CACHE_BYPASS)) {
        topic = "content-backing.store";
        rank = 0;
    }
    return flux_rpc_raw (h, topic, buf, len, rank, 0);
}

int flux_content_store_get (flux_future_t *f, const char **blobref)
{
    int ref_size;
    const char *ref;

    if (flux_rpc_get_raw (f, (const void **)&ref, &ref_size) < 0)
        return -1;
    if (!ref || ref[ref_size - 1] != '\0' || blobref_validate (ref) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (blobref)
        *blobref = ref;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
