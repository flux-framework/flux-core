#ifndef _FLUX_CORE_CONTENT_H
#define _FLUX_CORE_CONTENT_H

#include <flux/core.h>

/* flags */
enum {
    CONTENT_FLAG_CACHE_BYPASS = 1,/* request direct to backing store */
    CONTENT_FLAG_UPSTREAM = 2,    /* make request of upstream TBON peer */
};

/* Send request to load blob by blobref.
 */
flux_rpc_t *flux_content_load (flux_t *h, const char *blobref, int flags);

/* Get result of load request (blob).
 * This blocks until response is received.
 * Storage for 'buf' belongs to 'rpc' and is valid until 'rpc' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_load_get (flux_rpc_t *rpc, void *buf, int *len);

/* Send request to store blob.
 */
flux_rpc_t *flux_content_store (flux_t *h, const void *buf, int len, int flags);

/* Get result of store request (blobref).
 * Storage for 'blobref' belongs to 'rpc' and is valid until 'rpc' is destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_content_store_get (flux_rpc_t *rpc, const char **blobref);

#endif /* !_FLUX_CORE_CONTENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
