#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

#include "heartbeat.h"

typedef struct module_struct module_t;
typedef struct modhash_struct modhash_t;
typedef void (*modpoller_cb_f)(module_t *p, void *arg);
typedef void (*module_status_cb_f)(module_t *p, int prev_status, void *arg);

/* Hash-o-modules, keyed by uuid
 */
modhash_t *modhash_create (void);
void modhash_destroy (modhash_t *mh);

void modhash_set_rank (modhash_t *mh, uint32_t rank);
void modhash_set_flux (modhash_t *mh, flux_t *h);
void modhash_set_heartbeat (modhash_t *mh, heartbeat_t *hb);

/* Prepare module at 'path' for starting.
 */
module_t *module_add (modhash_t *mh, const char *path);
void module_remove (modhash_t *mh, module_t *p);

/* Set arguments to module main().  Call before module_start().
 */
void module_set_args (module_t *p, int argc, char * const argv[]);
void module_add_arg (module_t *p, const char *arg);

/* Get module name.
 */
const char *module_get_name (module_t *p);

/* Get module uuid.
 */
const char *module_get_uuid (module_t *p);

/* The poller callback is called when module socket is ready for
 * reading with module_recvmsg().
 */
void module_set_poller_cb (module_t *p, modpoller_cb_f cb, void *arg);

/* Send/recv a message for to/from a specific module.
 */
flux_msg_t *module_recvmsg (module_t *p);
int module_sendmsg (module_t *p, const flux_msg_t *msg);

/* Send an event message to all modules that have matching subscription.
 */
int module_event_mcast (modhash_t *mh, const flux_msg_t *msg);

/* Subscribe/unsubscribe module by uuid
 */
int module_subscribe (modhash_t *mh, const char *uuid, const char *topic);
int module_unsubscribe (modhash_t *mh, const char *uuid, const char *topic);

int module_push_rmmod (module_t *p, const flux_msg_t *msg);
flux_msg_t *module_pop_rmmod (module_t *p);
int module_push_insmod (module_t *p, const flux_msg_t *msg);
flux_msg_t *module_pop_insmod (module_t *p);

/* Get/set module status.
 */
void module_set_status (module_t *p, int status);
int module_get_status (module_t *p);
void module_set_status_cb (module_t *p, module_status_cb_f cb, void *arg);

int module_get_errnum (module_t *p);
void module_set_errnum (module_t *p, int errnum);

/* Send a response message to the module whose uuid matches the
 * next hop in the routing stack.
 */
int module_response_sendmsg (modhash_t *mh, const flux_msg_t *msg);

/* Find a module matching 'uuid'.
 */
module_t *module_lookup (modhash_t *mh, const char *uuid);

/* Find a module matching 'name'.
 * N.B. this is a slow linear search - keep out of crit paths
 */
module_t *module_lookup_byname (modhash_t *mh, const char *name);

/* Start module thread.
 */
int module_start (module_t *p);

/* Stop module thread by sending a shutdown request.
 */
int module_stop (module_t *p);

/* Prepare an 'lsmod' response payload.
 */
flux_modlist_t *module_get_modlist (modhash_t *mh);

#endif /* !_BROKER_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
