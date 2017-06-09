#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <stdbool.h>
#include <stdarg.h>

#include "handle.h"
#include "future.h"

enum {
    FLUX_RPC_NORESPONSE = 1,
};

typedef flux_future_t flux_rpc_t;

flux_future_t *flux_rpc (flux_t *h, const char *topic, const char *json_str,
                         uint32_t nodeid, int flags);

flux_future_t *flux_rpcf (flux_t *h, const char *topic, uint32_t nodeid,
                          int flags, const char *fmt, ...);

flux_future_t *flux_rpc_raw (flux_t *h, const char *topic,
                             const void *data, int len,
                             uint32_t nodeid, int flags);

int flux_rpc_get (flux_future_t *f, const char **json_str);

int flux_rpc_getf (flux_future_t *f, const char *fmt, ...);

int flux_rpc_get_raw (flux_future_t *f, void *data, int *len);


void flux_rpc_destroy (flux_future_t *f);

bool flux_rpc_check (flux_future_t *f);

int flux_rpc_then (flux_future_t *f, flux_continuation_f cb, void *arg);

void *flux_rpc_aux_get (flux_future_t *f, const char *name);
int flux_rpc_aux_set (flux_future_t *f, const char *name,
                      void *aux, flux_free_f destroy);

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
