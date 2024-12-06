/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CONTENT_H
#define _FLUX_CONTENT_H

/* flags */
enum {
    CONTENT_FLAG_CACHE_BYPASS = 1,/* request direct to backing store */
    CONTENT_FLAG_UPSTREAM = 2,    /* make request of upstream TBON peer */
};

/* Send request to load blob by hash or blobref.
 */
flux_future_t *content_load_byhash (flux_t *h,
                                    const void *hash,
                                    size_t hash_len,
                                    int flags);
flux_future_t *content_load_byblobref (flux_t *h,
                                       const char *blobref,
                                       int flags);

/* Get result of load request (blob).
 * This blocks until response is received.
 * Storage for 'buf' belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int content_load_get (flux_future_t *f, const void **buf, size_t *len);

/* Send request to store blob.
 */
flux_future_t *content_store (flux_t *h,
                              const void *buf,
                              size_t len,
                              int flags);

/* Get result of store request (hash or blobref).
 * Storage belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int content_store_get_hash (flux_future_t *f,
                            const void **hash,
                            size_t *hash_len);
int content_store_get_blobref (flux_future_t *f,
                               const char *hash_name,
                               const char **blobref);

#endif /* !_FLUX_CONTENT_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
