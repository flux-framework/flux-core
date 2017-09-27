#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <stdbool.h>
#include <stdarg.h>

#include "handle.h"
#include "future.h"

enum {
    FLUX_RPC_NORESPONSE = 1,
};

flux_future_t *flux_rpc (flux_t *h, const char *topic, const char *json_str,
                         uint32_t nodeid, int flags);

flux_future_t *flux_rpc_pack (flux_t *h, const char *topic, uint32_t nodeid,
                              int flags, const char *fmt, ...);

flux_future_t *flux_rpc_raw (flux_t *h, const char *topic,
                             const void *data, int len,
                             uint32_t nodeid, int flags);

int flux_rpc_get (flux_future_t *f, const char **json_str);

int flux_rpc_get_unpack (flux_future_t *f, const char *fmt, ...);

int flux_rpc_get_raw (flux_future_t *f, const void **data, int *len);


#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
