/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONTENT_H
#    define _FLUX_CORE_CONTENT_H

#    include <flux/core.h>

#    ifdef __cplusplus
extern "C" {
#    endif

/* flags */
enum {
    CONTENT_FLAG_CACHE_BYPASS = 1, /* request direct to backing store */
    CONTENT_FLAG_UPSTREAM = 2,     /* make request of upstream TBON peer */
};

/* Send request to load blob by blobref.
 */
flux_future_t *flux_content_load (flux_t *h, const char *blobref, int flags);

/* Get result of load request (blob).
 * This blocks until response is received.
 * Storage for 'buf' belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_load_get (flux_future_t *f, const void **buf, int *len);

/* Send request to store blob.
 */
flux_future_t *flux_content_store (flux_t *h,
                                   const void *buf,
                                   int len,
                                   int flags);

/* Get result of store request (blobref).
 * Storage for 'blobref' belongs to 'f' and is valid until 'f' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_store_get (flux_future_t *f, const char **blobref);

#    ifdef __cplusplus
}
#    endif

#endif /* !_FLUX_CORE_CONTENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
