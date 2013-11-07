#ifndef FLUX_H
#define FLUX_H

typedef struct flux_mrpc_struct *flux_mrpc_t;

/* Return the rank of the "local" node in the comms session.
 */
int flux_rank (void *h);

/* Return the number of nodes in the comms session.
 */
int flux_size (void *h);

/* Block until 'nprocs' processes make identical calls to flux_barrier().
 * 'name' should be unique, i.e. there should not be multiple sets of
 * processes potentially executing flux_barrier with the same name
 * concurrently!
 */
int flux_barrier (void *h, const char *name, int nprocs);

/* Send an event.  Events are strings of form
 *   event.topic.[subtopic[.subsubtopic...]]
 * JSON content is optional.
 */
int flux_event_send (void *h, json_object *o, const char *fmt, ...);

int flux_event_subscribe (void *h, const char *topic);
int flux_event_unsubscribe (void *h, const char *topic);

/* Singleton rpc call.
 *   N.B. if a specific node is desired, prepend "<nodeid>!" to tag.
 */
json_object *flux_rpc (void *h, json_object *in, const char *fmt, ...);

/* Group RPC - client
 */
flux_mrpc_t flux_mrpc_create (void *h, const char *nodelist);
void flux_mrpc_destroy (flux_mrpc_t f);

void flux_mrpc_put_inarg (flux_mrpc_t f, json_object *val);
int flux_mrpc_get_inarg (flux_mrpc_t f, json_object **valp);

void flux_mrpc_put_outarg (flux_mrpc_t f, char *node, json_object *val);
int flux_mrpc_get_outarg (flux_mrpc_t f, char *node, json_object **valp);

const char *flux_mrpc_next_outarg (flux_mrpc_t f); /* returns node */
void flux_mrpc_rewind_outarg (flux_mrpc_t f);

int flux_mrpc (flux_mrpc_t f, const char *fmt, ...);

/* Group RPC - server
 */


#endif /* !defined(FLUX_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
