#ifndef FLUX_H
#define FLUX_H

#include <czmq.h>

typedef void (FluxFreeFn)(void *arg);

typedef struct flux_handle_struct *flux_t;

#include "kvs.h"
#include "flux_log.h"

enum {
    FLUX_FLAGS_TRACE = 1,
};

typedef struct flux_mrpc_struct *flux_mrpc_t;

void flux_handle_destroy (flux_t *hp);
void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn *destroy);

int flux_request_sendmsg (flux_t h, zmsg_t **zmsg);
int flux_request_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_response_recvmsg (flux_t h, zmsg_t **zmsg, bool nb);
int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb);
int flux_response_putmsg (flux_t h, zmsg_t **zmsg);

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg);
int flux_event_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_event_recvmsg (flux_t h, zmsg_t **zmsg, bool nonblock);
int flux_event_subscribe (flux_t h, const char *topic);
int flux_event_unsubscribe (flux_t h, const char *topic);

int flux_snoop_recvmsg (flux_t h, zmsg_t **zmsg, bool nb);
int flux_snoop_subscribe (flux_t h, const char *topic);
int flux_snoop_unsubscribe (flux_t h, const char *topic);

int flux_rank (flux_t h);
int flux_size (flux_t h);

int flux_barrier (flux_t h, const char *name, int nprocs);

void flux_flags_set (flux_t h, int flags);
void flux_flags_unset (flux_t h, int flags);

json_object *flux_rpc (flux_t h, json_object *in, const char *fmt, ...);

/* Group RPC
 *
 * Client:                          Servers:
 *   flux_mrpc_create()               flux_event_subscribe ("mrpc...")
 *   flux_mrpc_put_inarg()            while (true) {
 *   flux_mrpc() ------------------->   (receive event)
 *                                      flux_mrpc_create_fromevent()
 *                                      flux_mrpc_get_inarg()
 *                                      (do some work)
 *                                      flux_mrpc_put_outarg()
 *   (returns) <----------------------- flux_mrpc_respond()
 * - flux_mrpc_get_outarg() ...         flux_mrpc_destroy() 
 * - flux_mrpc_destroy()              }
 */
flux_mrpc_t flux_mrpc_create (flux_t h, const char *nodelist);
void flux_mrpc_destroy (flux_mrpc_t f);

void flux_mrpc_put_inarg (flux_mrpc_t f, json_object *val);
int flux_mrpc_get_inarg (flux_mrpc_t f, json_object **valp);

void flux_mrpc_put_outarg (flux_mrpc_t f, json_object *val);
int flux_mrpc_get_outarg (flux_mrpc_t f, int nodeid, json_object **valp);

/* returns nodeid (-1 at end)  */
int flux_mrpc_next_outarg (flux_mrpc_t f);
void flux_mrpc_rewind_outarg (flux_mrpc_t f);

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...);

/* returns NULL, errno == EINVAL if not addressed to me */
flux_mrpc_t flux_mrpc_create_fromevent (flux_t h, json_object *request);
int flux_mrpc_respond (flux_mrpc_t f);

#endif /* !defined(FLUX_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
