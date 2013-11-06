#ifndef COMMS_H
#define COMMS_H

typedef struct flux_rpc_struct *flux_rpc_t;

/* Return the rank of the "local" node in the comms session.
 */
int flux_rank (void *h);

/* Return the number of nodes in the comms session.
 */
int flux_size (void *h);

/* Send an event.  Events are strings of form
 *   event.topic.[subtopic[.subsubtopic...]]
 */
int flux_event_send (void *h, const char *fmt, ...);

/* Singleton rpc call.
 *   N.B. if a specific node is desired, prepend "<nodeid>!" to tag.
 */
json_object *flux_rpc (void *h, json_object *in, const char *fmt, ...);

/* Group RPC.
 */
void flux_rpc_create (void *h, const char *nodelist, flux_rpc_t *fp);
void flux_rpc_destroy (flux_rpc_t f);

void flux_rpc_put_inarg (flux_rpc_t f, json_object *val);
int flux_rpc_get_inarg (flux_rpc_t f, json_object **valp);

void flux_rpc_put_outarg (flux_rpc_t f, char *node, json_object *val);
int flux_rpc_get_outarg (flux_rpc_t f, char *node, json_object **valp);

const char *flux_rpc_next_outarg (flux_rpc_t f); /* returns node */
void flux_rpc_rewind_outarg (flux_rpc_t f);

int flux_mrpc (flux_rpc_t f, const char *fmt, ...);

#endif /* !defined(COMMS_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
