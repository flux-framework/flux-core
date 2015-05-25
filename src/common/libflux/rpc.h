#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"
#include "request.h"

typedef struct flux_rpc_struct *flux_rpc_t;
typedef void (*flux_then_f)(flux_rpc_t rpc, void *arg);

flux_rpc_t flux_rpc (flux_t h, const char *topic, const char *json_str,
                     uint32_t nodeid, int flags);
void flux_rpc_destroy (flux_rpc_t rpc);

bool flux_rpc_check (flux_rpc_t rpc);
int flux_rpc_get (flux_rpc_t rpc, uint32_t *nodeid, const char **json_str);

int flux_rpc_then (flux_rpc_t rpc, flux_then_f cb, void *arg);

flux_rpc_t flux_multrpc (flux_t h, const char *topic, const char *json_str,
                         const char *nodeset, int flags);
bool flux_rpc_completed (flux_rpc_t rpc);

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
