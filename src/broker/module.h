/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_MODULE_H
#define _BROKER_MODULE_H

#include <jansson.h>

#include "src/common/librouter/disconnect.h"

#include "attr.h"
#include "service.h"

typedef struct broker_module module_t;
typedef struct modhash modhash_t;
typedef void (*modpoller_cb_f)(module_t *p, void *arg);
typedef void (*module_status_cb_f)(module_t *p, int prev_status, void *arg);

/* Hash-o-modules, keyed by uuid
 */
modhash_t *modhash_create (void);
void modhash_destroy (modhash_t *mh);

void modhash_initialize (modhash_t *mh,
                         flux_t *h,
                         const char *uuid,
                         attr_t *attrs);

/* Prepare module at 'path' for starting.
 */
module_t *module_add (modhash_t *mh,
                      const char *name, // may be NULL
                      const char *path,
                      json_t *args,
                      flux_error_t *error);
void module_remove (modhash_t *mh, module_t *p);

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

/* Pass module's requests through this function to enable disconnect
 * messages to be sent when the module is unloaded.  The callback will
 * be used to send those messages.
 */
int module_disconnect_arm (module_t *p,
                           const flux_msg_t *msg,
                           disconnect_send_f cb,
                           void *arg);

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
 * Either the module name or the path given to module_add() works.
 * N.B. this is a slow linear search - keep out of crit paths
 */
module_t *module_lookup_byname (modhash_t *mh, const char *name);

/* Start module thread.
 */
int module_start (module_t *p);

/* Stop module thread by sending a shutdown request.
 */
int module_stop (module_t *p);

/*  Mute module. Do not send any more messages.
 */
void module_mute (module_t *p);

/* Prepare RFC 5 'mods' array for lsmod response.
 */
json_t *module_get_modlist (modhash_t *mh, struct service_switch *sw);

/* Iterator
 */
module_t *module_first (modhash_t *mh);
module_t *module_next (modhash_t *mh);

#endif /* !_BROKER_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
