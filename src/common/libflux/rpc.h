#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"
#include "request.h"

typedef struct flux_mresponse_struct *flux_mresponse_t;


/* Synchronous request/response.
 * If FLUX_O_COPROC is set, this call can sleep and let other reactor
 * callbacks be serviced, otherwise it blocks until the response is received.
 * If the response contains a nonzero errnum, this becomes the errno of
 * the flux_rpc call.  If 'response' is non-NULL, the response message
 * is assigned to it for further decoding (caller must destroy).
 * Returns 0 on success, or -1 on failure with errno set.
 */

int flux_rpc (flux_t h, const char *topic,
              const char *json_str, zmsg_t **response);
int flux_rpcto (flux_t h, const char *topic,
                const char *json_str, zmsg_t **response, uint32_t nodeid);

/* Bulk synchronous request/response.
 * If FLUX_O_COPROC is set, this call can sleep and let other reactor
 * callbacks be serviced, otherwise it blocks until all responses are received.
 * 'fanout' limits the number of concurrent requests (0 == unlimited).
 * If 'r' is non-NULL, it is assigned to a flux_mresponse_t that can be
 * used to decode individual responses (caller must destroy).  If 'r' is
 * NULL, the responses are decoded internally to detect any failure responses,
 * but response payloads, if any, are discarded.  If any failures are detected,
 * flux_multrpc fails with errno = the largest response errnum.
 * Returns 0 on success, or -1 on failure with errno set.
 */

int flux_multrpcto (flux_t h, int fanout,
                   const char *topic, const char *json_str,
                   flux_mresponse_t *r, const char *nodeset);

int flux_multrpc (flux_t h, int fanout,
                  const char *topic, const char *json_str,
                  flux_mresponse_t *r);

int flux_mresponse_decode (flux_mresponse_t r, uint32_t nodeid,
                           const char **topic, const char **json_str);

void flux_mresponse_destroy (flux_mresponse_t r);

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
