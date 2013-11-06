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

#endif /* !defined(COMMS_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
