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
#include "config.h"
#endif
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <flux/core.h>

#include "content.h"

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/errno_safe.h"

flux_future_t *content_load_byhash (flux_t *h,
                                    const void *hash,
                                    size_t hash_size,
                                    int flags)
{
    const char *topic = "content.load";
    uint32_t rank = FLUX_NODEID_ANY;

    if (!h || !hash) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & CONTENT_FLAG_UPSTREAM))
        rank = FLUX_NODEID_UPSTREAM;
    if ((flags & CONTENT_FLAG_CACHE_BYPASS)) {
        topic = "content-backing.load";
        rank = 0;
    }
    return flux_rpc_raw (h, topic, hash, hash_size, rank, 0);
}

flux_future_t *content_load_byblobref (flux_t *h,
                                       const char *blobref,
                                       int flags)
{
    uint32_t hash[BLOBREF_MAX_DIGEST_SIZE];
    ssize_t hash_size;

    if ((hash_size = blobref_strtohash (blobref, hash, sizeof (hash))) < 0)
        return NULL;
    return content_load_byhash (h, hash, hash_size, flags);
}

int content_load_get (flux_future_t *f, const void **buf, size_t *len)
{
    return flux_rpc_get_raw (f, buf, len);
}

flux_future_t *content_store (flux_t *h, const void *buf, size_t len, int flags)
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

int content_store_get_hash (flux_future_t *f,
                            const void **hash,
                            size_t *hash_size)
{
    const void *buf;
    size_t buf_size;

    if (flux_rpc_get_raw (f, &buf, &buf_size) < 0)
        return -1;
    if (hash)
        *hash = buf;
    if (hash_size)
        *hash_size = buf_size;
    return 0;
}

int content_store_get_blobref (flux_future_t *f,
                               const char *hash_name,
                               const char **blobref)
{
    const char *auxkey = "flux::blobref";
    const char *result;

    if (!(result = flux_future_aux_get (f, auxkey))) {
        const void *hash;
        size_t hash_len;
        char buf[BLOBREF_MAX_STRING_SIZE];
        char *cpy = NULL;

        if (content_store_get_hash (f, &hash, &hash_len) < 0
            || blobref_hashtostr (hash_name,
                                  hash,
                                  hash_len,
                                  buf,
                                  sizeof (buf)) < 0
            || !(cpy = strdup (buf))
            || flux_future_aux_set (f, auxkey, cpy, (flux_free_f)free) < 0) {
            ERRNO_SAFE_WRAP (free, cpy);
            return -1;
        }
        result = cpy;
    }
    if (blobref)
        *blobref = result;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
