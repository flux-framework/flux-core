/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_SHUTDOWN_H
#define _BROKER_SHUTDOWN_H

/* Manage the shutdown process for the comms session.
 *
 * Design:
 * The broker registers a shutdown callback.
 *
 * On receipt of a "shutdown" event, the grace timer is armed, and
 * the broker callback is called with 'expired' false.  The broker
 * should initiate its clean shutdown path.  If the clean shutdown path
 * succeedes, the broker calls shutdown_disarm() to disarm the timer.
 *
 * If the grace timer expires before then, the broker callback
 * is called with 'exipred' true.
 */

typedef struct shutdown_struct shutdown_t;
typedef void (*shutdown_cb_f)(shutdown_t *s, bool expired, void *arg);

/* Create/destroy shutdown_t.
 */
shutdown_t *shutdown_create (void);
void shutdown_destroy (shutdown_t *s);

/* Set the flux_t *handle to be used to configure the event message
 * handler, grace timer watcher, and log the shutdown message.
 */
void shutdown_set_handle (shutdown_t *s, flux_t *h);

/* Reigster a shutdown callback to be called
 * 1) when the grace timeout is armed, and
 * 2) when the grace timeout expires.
 */
void shutdown_set_callback (shutdown_t *s, shutdown_cb_f cb, void *arg);

/* Shutdown callback may call this to obtain the broker exit code
 * encoded in the shutdown event.
 */
int shutdown_get_rc (shutdown_t *s);

/* Call shutdown_arm() when shutdown should begin.
 * This sends the "cmb.shutdown" event to all ranks.
 */
int shutdown_arm (shutdown_t *s, double grace, int rc, const char *fmt, ...);

/* Call shutdown_disarm() once the clean shutdown path has succeeded.
 * This disarms the timer on the local rank only.
 */
void shutdown_disarm (shutdown_t *s);

/* Shutdown event encode/decode
 * (used internally, exposed for testing)
 */
flux_msg_t *shutdown_vencode (double grace, int rc, int rank,
                              const char *fmt, va_list ap);
flux_msg_t *shutdown_encode (double grace, int rc, int rank,
                             const char *fmt, ...);
int shutdown_decode (const flux_msg_t *msg, double *grace, int *rc, int *rank,
                     char *reason, int reason_len);


#endif /* !_BROKER_SHUTDOWN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
