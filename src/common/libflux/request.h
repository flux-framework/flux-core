#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"

/* Send/receive requests and responses.
 * - flux_request_sendmsg() expects a route delimiter (request envelope)
 * - flux_request_sendmsg()/flux_response_sendmsg() free '*zmsg' and set it
 *   to NULL on success.
 * - int-returning functions return 0 on success, -1 on failure with errno set.
 * - pointer-returning functions return NULL on failure with errno set.
 */
int flux_request_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_request_recvmsg (flux_t h, bool nb);
int flux_response_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_response_recvmsg (flux_t h, bool nb);
int flux_response_putmsg (flux_t h, zmsg_t **zmsg);
int flux_request_send (flux_t h, json_object *request, const char *fmt, ...);
zmsg_t *flux_response_matched_recvmsg (flux_t h, const char *match, bool nb);
json_object *flux_rpc (flux_t h, json_object *in, const char *fmt, ...);
int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb);
int flux_respond (flux_t h, zmsg_t **request, json_object *response);
int flux_respond_errnum (flux_t h, zmsg_t **request, int errnum);

/* Send a request to a particular cmb rank
 *   otherwise identical to flux_request_sendmsg()/flux_request_send()
 */
int flux_rank_request_sendmsg (flux_t h, int rank, zmsg_t **zmsg);
int flux_rank_request_send (flux_t h, int rank,
                            json_object *request, const char *fmt, ...);
json_object *flux_rank_rpc (flux_t h, int rank,
                            json_object *in, const char *fmt, ...);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
