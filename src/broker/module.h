#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

typedef struct module_struct *module_t;
typedef struct modhash_struct *modhash_t;
typedef void (*modpoller_cb_f)(module_t p, void *arg);
typedef void (*rmmod_cb_f)(module_t p, void *arg);

/* Hash-o-modules, keyed by uuid
 */
modhash_t modhash_create (void);
void modhash_destroy (modhash_t mh);

void modhash_set_zctx (modhash_t mh, zctx_t *zctx);
void modhash_set_rank (modhash_t mh, uint32_t rank);
void modhash_set_loop (modhash_t mh, zloop_t *zloop);
void modhash_set_heartbeat (modhash_t mh, heartbeat_t hb);

/* Prepare module at 'path' for starting.
 */
module_t module_add (modhash_t mh, const char *path);
void module_remove (modhash_t mh, module_t p);

/* Set arguments to module main().  Call before module_start().
 */
void module_set_args (module_t p, int argc, char * const argv[]);
void module_add_arg (module_t p, const char *arg);

/* Get/set module name.
 * If unset, use the value of 'mod_name' compiled into the module .so.
 */
const char *module_get_name (module_t p);
void module_set_name (module_t p, const char *name);

/* The poller callback is called when module socket is ready for
 * reading with module_recvmsg().
 */
void module_set_poller_cb (module_t p, modpoller_cb_f cb, void *arg);

/* Send/recv a message for to/from a specific module.
 */
zmsg_t *module_recvmsg (module_t p);
int module_sendmsg (zmsg_t **zmsg, module_t p);

/* Send an event message to all modules that have matching subscription.
 */
int module_event_mcast (modhash_t mh, zmsg_t *zmsg);

/* Subscribe/unsubscribe module by uuid
 */
int module_subscribe (modhash_t mh, const char *uuid, const char *topic);
int module_unsubscribe (modhash_t mh, const char *uuid, const char *topic);

/* The rmmod callback is called as part of module destruction,
 * after the thread has been joined, so that module_pop_rmmod() can
 * be called to obtain any queued rmmod requests that now need
 * their replies.
 */
void module_set_rmmod_cb (module_t p, rmmod_cb_f cb, void *arg);
zmsg_t *module_pop_rmmod (module_t p);

/* Send a response message to the module whose uuid matches the
 * next hop in the routing stack.
 */
int module_response_sendmsg (modhash_t mh, zmsg_t **zmsg);

/* Find a module matching 'name'.
 * N.B. this is a slow linear search - keep out of crit paths
 */
module_t module_lookup_byname (modhash_t mh, const char *name);

/* Start module thread.
 */
int module_start (module_t p);
int module_start_all (modhash_t mh);

/* Stop module thread by sending a shutdown request.
 * If stop was instigated by an rmmod request, queue the request here
 * for reply once the module actually stops.
 */
int module_stop (module_t p, zmsg_t **zmsg);
int module_stop_all (modhash_t mh);

/* Prepare an 'lsmod' response payload.
 */
json_object *module_list_encode (modhash_t mh);

#endif /* !_BROKER_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
