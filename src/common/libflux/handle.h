/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_HANDLE_H
#define _FLUX_CORE_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "types.h"
#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_handle flux_t;

typedef struct {
    int request_tx;
    int request_rx;
    int response_tx;
    int response_rx;
    int event_tx;
    int event_rx;
    int control_tx;
    int control_rx;
} flux_msgcounters_t;

typedef int (*flux_comms_error_f)(flux_t *h, void *arg);

/* Flags for handle creation and flux_flags_set()/flux_flags_unset.
 */
enum {
    FLUX_O_TRACE = 1,       /* send message trace to stderr */
    FLUX_O_CLONE = 2,       /* handle was created with flux_clone() */
    FLUX_O_NONBLOCK = 4,    /* handle should not block on send/recv */
    FLUX_O_MATCHDEBUG = 8,  /* enable matchtag debugging */
    FLUX_O_TEST_NOSUB = 16, /* for testing: make (un)subscribe a no-op */
    FLUX_O_RPCTRACK = 32,   /* track RPCs for recovery after reconnect */
    FLUX_O_NOREQUEUE = 64,  /* disable flux_requeue() to save file descr. */
};

/* Flags for flux_requeue().
 */
enum {
    FLUX_RQ_HEAD = 1,   /* requeue message at head of queue */
    FLUX_RQ_TAIL = 2,   /* requeue message at tail of queue */
};

/* Flags for flux_pollevents().
 */
enum {
    FLUX_POLLIN = 1,
    FLUX_POLLOUT = 2,
    FLUX_POLLERR = 4,
};

/* Options for flux_opt_set().
 * (Connectors may define custom option names)
 */
#define FLUX_OPT_TESTING_USERID     "flux::testing_userid"
#define FLUX_OPT_TESTING_ROLEMASK   "flux::testing_rolemask"
#define FLUX_OPT_ROUTER_NAME        "flux::router_name"
#define FLUX_OPT_SEND_QUEUE_COUNT   "flux::send_queue_count"
#define FLUX_OPT_RECV_QUEUE_COUNT   "flux::recv_queue_count"

/* Create/destroy a broker handle.
 * The 'uri' scheme name selects a connector to dynamically load.
 * The rest of the URI is parsed in an connector-specific manner.
 * A NULL uri selects the "local" connector with path stored
 * in the environment variable FLUX_URI.
 */
flux_t *flux_open (const char *uri, int flags);

/* Like flux_open(), but if optional flux_error_t parameter is non-NULL,
 * then any errors normally emitted to stderr will instead be returned
 * in error->text.
 */
flux_t *flux_open_ex (const char *uri, int flags, flux_error_t *error);

void flux_close (flux_t *h);

/* Increment internal reference count on 'h'.
 */
flux_t *flux_incref (flux_t *h);
void flux_decref(flux_t *h);

/* Create a new handle that is an alias for 'orig' in all respects
 * except it adds FLUX_O_CLONE to flags and has its own 'aux' hash
 * (which means it has its own reactor and dispatcher).
 */
flux_t *flux_clone (flux_t *orig);

/* Drop connection to broker and re-establish, if supported by connector.
 */
int flux_reconnect (flux_t *h);

/* Get/set handle options.  Options are interpreted by connectors.
 * Returns 0 on success, or -1 on failure with errno set (e.g. EINVAL).
 */
int flux_opt_set (flux_t *h, const char *option, const void *val, size_t len);
int flux_opt_get (flux_t *h, const char *option, void *val, size_t len);

/* Register a callback to flux_send() / flux_recv() errors.
 * The callback return value becomes the send/receive function return value.
 */
void flux_comms_error_set (flux_t *h, flux_comms_error_f fun, void *arg);

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The destructor, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 * Key names used internally by flux-core are prefixed with "flux::".
 *
 * N.B. flux_aux_get does not scale to a large number of items, and
 * broker module handles may persist for a long time.
 */
void *flux_aux_get (flux_t *h, const char *name);
int flux_aux_set (flux_t *h, const char *name, void *aux, flux_free_f destroy);

/* Set/clear FLUX_O_* on a flux_t handle.
 */
void flux_flags_set (flux_t *h, int flags);
void flux_flags_unset (flux_t *h, int flags);
int flux_flags_get (flux_t *h);

/* Alloc/free matchtag for matched request/response.
 * This is mainly used internally by the rpc code.
 */
uint32_t flux_matchtag_alloc (flux_t *h);
void flux_matchtag_free (flux_t *h, uint32_t matchtag);
uint32_t flux_matchtag_avail (flux_t *h);

/* Send a message
 * flags may be 0 or FLUX_O_TRACE or FLUX_O_NONBLOCK (FLUX_O_COPROC is ignored)
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_send (flux_t *h, const flux_msg_t *msg, int flags);

/* Send a message - same as above but '*msg' ownership is transferred to 'h'.
 * N.B. this fails with EINVAL if '*msg' reference count is greater than 1.
 */
int flux_send_new (flux_t *h, flux_msg_t **msg, int flags);

/* Receive a message
 * flags may be 0 or FLUX_O_TRACE or FLUX_O_NONBLOCK (FLUX_O_COPROC is ignored)
 * flux_recv reads messages from the handle until 'match' is matched,
 * then requeues any non-matching messages.
 * Returns message on success, NULL on failure.
 * The message must be destroyed with flux_msg_destroy().
 */
flux_msg_t *flux_recv (flux_t *h, struct flux_match match, int flags);

/* Requeue a message
 * flags must be either FLUX_RQ_HEAD or FLUX_RQ_TAIL.
 * A message that is requeued will be seen again by flux_recv() and will
 * cause FLUX_POLLIN to be raised in flux_pollevents().
 */
int flux_requeue (flux_t *h, const flux_msg_t *msg, int flags);

/* Obtain a bitmask of FLUX_POLL* bits for the flux handle.
 * Returns bitmask on success, -1 on error with errno set.
 * See flux_pollfd() comment below.
 */
int flux_pollevents (flux_t *h);

/* Obtain a file descriptor that can be used to integrate a flux_t handle
 * into an external event loop.  When one of FLUX_POLLIN, FLUX_POLLOUT, or
 * FLUX_POLLERR is raised in flux_pollevents(), this file descriptor will
 * become readable in an edge triggered fashion.  The external event loop
 * should then call flux_pollevents().  See src/common/libflux/ev_flux.[ch]
 * for an example of a libev "composite watcher" based on these interfaces,
 * that is used internally by the flux reactor.
 * Returns fd on success, -1 on failure with errno set.
 */
int flux_pollfd (flux_t *h);

/* Get/clear handle message counters.
 */
void flux_get_msgcounters (flux_t *h, flux_msgcounters_t *mcs);
void flux_clr_msgcounters (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
