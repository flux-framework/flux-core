#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include "handle.h"
#include "future.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FLUX_RPC_NORESPONSE = 1,
};

flux_future_t *flux_rpc (flux_t *h, const char *topic, const char *s,
                         uint32_t nodeid, int flags);

flux_future_t *flux_rpc_pack (flux_t *h, const char *topic, uint32_t nodeid,
                              int flags, const char *fmt, ...);

flux_future_t *flux_rpc_raw (flux_t *h, const char *topic,
                             const void *data, int len,
                             uint32_t nodeid, int flags);

int flux_rpc_get (flux_future_t *f, const char **s);

int flux_rpc_get_unpack (flux_future_t *f, const char *fmt, ...);

int flux_rpc_get_raw (flux_future_t *f, const void **data, int *len);

/* Get a human-readable error message for fulfilled RPC.
 * The result is always a valid string:
 * If the RPC did not fail, flux_strerror (0) is returned.
 * If the RPC failed, but did not include an error message payload,
 * flux_strerror (errnum) is returned.
 */
const char *flux_rpc_get_error (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
