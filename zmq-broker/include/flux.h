#ifndef FLUX_H
#define FLUX_H

#include <czmq.h>

typedef void (*FluxFreeFn)(void *arg);

typedef struct flux_handle_struct *flux_t;

#include "kvs.h"

enum {
    FLUX_FLAGS_TRACE = 1,
};

typedef struct flux_mrpc_struct *flux_mrpc_t;

void flux_handle_destroy (flux_t *hp);

void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy);

void flux_flags_set (flux_t h, int flags);
void flux_flags_unset (flux_t h, int flags);

int flux_request_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_request_recvmsg (flux_t h, bool nb);
int flux_response_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_response_recvmsg (flux_t h, bool nb);
int flux_response_putmsg (flux_t h, zmsg_t **zmsg);
int flux_request_send (flux_t h, json_object *request, const char *fmt, ...);
json_object *flux_rpc (flux_t h, json_object *in, const char *fmt, ...);
int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb);
int flux_respond (flux_t h, zmsg_t **request, json_object *response);
int flux_respond_errnum (flux_t h, zmsg_t **request, int errnum);

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_event_recvmsg (flux_t h, bool nonblock);
int flux_event_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_event_subscribe (flux_t h, const char *topic);
int flux_event_unsubscribe (flux_t h, const char *topic);

zmsg_t *flux_snoop_recvmsg (flux_t h, bool nb);
int flux_snoop_subscribe (flux_t h, const char *topic);
int flux_snoop_unsubscribe (flux_t h, const char *topic);

int flux_rank (flux_t h);
int flux_size (flux_t h);
bool flux_treeroot (flux_t h);

int flux_timeout_set (flux_t h, unsigned long msec);
int flux_timeout_clear (flux_t h);
bool flux_timeout_isset (flux_t h);

zloop_t *flux_get_zloop (flux_t h);
zctx_t *flux_get_zctx (flux_t h);

int flux_barrier (flux_t h, const char *name, int nprocs);

void flux_log_set_facility (flux_t h, const char *facility);
int flux_vlog (flux_t h, int lev, const char *fmt, va_list ap);
int flux_log (flux_t h, int lev, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

int flux_log_subscribe (flux_t h, int lev, const char *sub);
int flux_log_unsubscribe (flux_t h, const char *sub);
int flux_log_dump (flux_t h, int lev, const char *fac);

char *flux_log_decode (zmsg_t *zmsg, int *lp, char **fp, int *cp,
                    struct timeval *tvp, char **sp);

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
