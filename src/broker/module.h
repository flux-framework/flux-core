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
#include <flux/core.h>

#include "src/common/librouter/disconnect.h"

typedef struct broker_module module_t;
typedef void (*modpoller_cb_f)(module_t *p, void *arg);
typedef void (*module_status_cb_f)(module_t *p, int prev_status, void *arg);

module_t *module_create (flux_t *h,
                         const char *parent_uuid,
                         const char *name, // may be NULL
                         const char *path,
                         int rank,
                         json_t *args,
                         flux_error_t *error);
void module_destroy (module_t *p);

/* accessors
 */
const char *module_get_name (module_t *p);
const char *module_get_path (module_t *p);
const char *module_get_uuid (module_t *p);
double module_get_lastseen (module_t *p);

/* Associate aux data with a module.
 */
void *module_aux_get (module_t *p, const char *name);
int module_aux_set (module_t *p,
                    const char *name,
                    void *val,
                    flux_free_f destroy);

/* The poller callback is called when module socket is ready for
 * reading with module_recvmsg().
 */
void module_set_poller_cb (module_t *p, modpoller_cb_f cb, void *arg);

/* Send/recv a message for to/from a specific module.
 */
flux_msg_t *module_recvmsg (module_t *p);
int module_sendmsg_new (module_t *p, flux_msg_t **msg);

/* Pass module's requests through this function to enable disconnect
 * messages to be sent when the module is unloaded.  The callback will
 * be used to send those messages.
 */
int module_disconnect_arm (module_t *p,
                           const flux_msg_t *msg,
                           disconnect_send_f cb,
                           void *arg);

int module_push_rmmod (module_t *p, const flux_msg_t *msg);
const flux_msg_t *module_pop_rmmod (module_t *p);
int module_push_insmod (module_t *p, const flux_msg_t *msg);
const flux_msg_t *module_pop_insmod (module_t *p);

/* Get/set module status.
 */
void module_set_status (module_t *p, int status);
int module_get_status (module_t *p);
void module_set_status_cb (module_t *p, module_status_cb_f cb, void *arg);

int module_get_errnum (module_t *p);
void module_set_errnum (module_t *p, int errnum);

/* Start module thread.
 */
int module_start (module_t *p);

/* Stop module thread by sending a shutdown request.
 */
int module_stop (module_t *p, flux_t *h);

/* Defer all messages that would be sent to module if flag=true.
 * Stop deferring them and send backlog if flag=false.
 */
int module_set_defer (module_t *p, bool flag);

/*  Mute module. Do not send any more messages.
 */
void module_mute (module_t *p);

/* Call pthread_cancel() on module.
 */
int module_cancel (module_t *p, flux_error_t *error);

/* Manage module subscriptions.
 */
int module_subscribe (module_t *p, const char *topic);
int module_unsubscribe (module_t *p, const char *topic);
int module_event_cast (module_t *p, const flux_msg_t *msg);

ssize_t module_get_send_queue_count (module_t *p);
ssize_t module_get_recv_queue_count (module_t *p);

#endif /* !_BROKER_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
