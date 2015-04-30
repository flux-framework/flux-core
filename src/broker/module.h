#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

/* Templates used to construct module socket URIs
 */
#define MODEVENT_INPROC_URI           "inproc://event"
#define SVC_INPROC_URI_TMPL           "inproc://svc-%s"

typedef struct module_struct *module_t;
typedef struct modhash_struct *modhash_t;
typedef void (*modpoller_cb_f)(module_t p, void *arg);
typedef void (*rmmod_cb_f)(module_t p, void *arg);

/* Hash-o-modules, keyed by both uuid and name.
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

/* Look up module by name.
 */
module_t module_lookup (modhash_t mh, const char *name);

/* Set arguments to module main().  Call before module_start().
 */
void module_set_args (module_t p, int argc, char * const argv[]);
void module_add_arg (module_t p, const char *arg);

const char *module_name (module_t p);

/* The poller callback is called when module socket is ready for
 * reading with module_recvmsg().
 */
void module_set_poller_cb (module_t p, modpoller_cb_f cb, void *arg);
zmsg_t *module_recvmsg (module_t p);

/* The rmmod callback is called as part of module destruction,
 * after the thread has been joined, so that module_pop_rmmod() can
 * be called to obtain any queued rmmod requests that now need
 * their replies.
 */
void module_set_rmmod_cb (module_t p, rmmod_cb_f cb, void *arg);
zmsg_t *module_pop_rmmod (module_t p);

/* Send a request message to the module whose name matches the
 * (truncated) topic string of the request.
 */
int module_request_sendmsg (modhash_t mh, zmsg_t **zmsg);

/* Send a response message to the module whose uuid matches the
 * next hop in the routing stack.
 */
int module_response_sendmsg (modhash_t mh, zmsg_t **zmsg);

/* Start module thread.
 */
int module_start (module_t p);

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
