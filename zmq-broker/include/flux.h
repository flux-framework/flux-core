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
 * processes executing flux_barrier() with the same name concurrently!
 */
int flux_barrier (void *h, const char *name, int nprocs);

/* Send an event.  Event tags are strings of form
 *   event.topic.[subtopic[.subsubtopic...]]
 * the JSON part can be NULL.
 */
int flux_event_send (void *h, json_object *o, const char *fmt, ...);
int flux_event_subscribe (void *h, const char *topic);
int flux_event_unsubscribe (void *h, const char *topic);

/* Enable/disable snooping on request/response messages passing
 * through the local cmb.
 */
int flux_snoop_subscribe (void *h, const char *topic);
int flux_snoop_unsubscribe (void *h, const char *topic);

/* Singleton rpc call.
 *   N.B. if a specific node is desired, prepend "<nodeid>!" to tag.
 */
json_object *flux_rpc (void *h, json_object *in, const char *fmt, ...);

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
flux_mrpc_t flux_mrpc_create (void *h, const char *nodelist);
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
flux_mrpc_t flux_mrpc_create_fromevent (void *h, json_object *request);
int flux_mrpc_respond (flux_mrpc_t f);

#endif /* !defined(FLUX_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
