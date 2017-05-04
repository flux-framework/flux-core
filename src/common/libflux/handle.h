#ifndef _FLUX_CORE_HANDLE_H
#define _FLUX_CORE_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "types.h"
#include "message.h"

typedef struct flux_handle_struct flux_t;

typedef struct {
    int request_tx;
    int request_rx;
    int response_tx;
    int response_rx;
    int event_tx;
    int event_rx;
    int keepalive_tx;
    int keepalive_rx;
} flux_msgcounters_t;

typedef void (*flux_fatal_f)(const char *msg, void *arg);

/* Flags for handle creation and flux_flags_set()/flux_flags_unset.
 */
enum {
    FLUX_O_TRACE = 1,   /* send message trace to stderr */
    FLUX_O_COPROC = 2,  /* start reactor callbacks as coprocesses */
    FLUX_O_NONBLOCK = 4,/* handle should not block on send/recv */
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

/* Flags for flux_matchtag_alloc()
 */
enum {
    FLUX_MATCHTAG_GROUP = 1,
};

/* Options for flux_setopt().
 * (Connectors may define custom option names)
 */
#define FLUX_OPT_TESTING_USERID     "flux::testing_userid"
#define FLUX_OPT_TESTING_ROLEMASK   "flux::testing_rolemask"

/* Create/destroy a broker handle.
 * The 'uri' scheme name selects a connector to dynamically load.
 * The rest of the URI is parsed in an connector-specific manner.
 * A NULL uri selects the "local" connector with path stored
 * in the environment variable FLUX_URI.
 */
flux_t *flux_open (const char *uri, int flags);
void flux_close (flux_t *h);

/* Increment internal reference count on 'h'.
 */
void flux_incref (flux_t *h);

/* Get/set handle options.  Options are interpreted by connectors.
 * Returns 0 on success, or -1 on failure with errno set (e.g. EINVAL).
 */
int flux_opt_set (flux_t *h, const char *option, const void *val, size_t len);
int flux_opt_get (flux_t *h, const char *option, void *val, size_t len);

/* Register a handler for fatal handle errors.
 * A fatal error is ENOMEM or a handle send/recv error after which
 * it is inadvisable to continue using the handle.
 */
void flux_fatal_set (flux_t *h, flux_fatal_f fun, void *arg);

/* Set the fatality bit and call the user's fatal error handler, if any.
 * The fatal error handler will only be called once.
 */
void flux_fatal_error (flux_t *h, const char *fun, const char *msg);
#define FLUX_FATAL(h) do { \
    flux_fatal_error((h),__FUNCTION__,(strerror (errno))); \
} while (0)

/* Return true if the handle 'h' has encountered a fatal error.
 */
bool flux_fatality (flux_t *h);

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The destructor, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 * Key names used internally by flux-core are prefixed with "flux::".
 */
void *flux_aux_get (flux_t *h, const char *name);
void flux_aux_set (flux_t *h, const char *name, void *aux, flux_free_f destroy);

/* Set/clear FLUX_O_* on a flux_t handle.
 */
void flux_flags_set (flux_t *h, int flags);
void flux_flags_unset (flux_t *h, int flags);
int flux_flags_get (flux_t *h);

/* Alloc/free matchtag for matched request/response.
 * This is mainly used internally by the rpc code.
 */
uint32_t flux_matchtag_alloc (flux_t *h, int flags);
void flux_matchtag_free (flux_t *h, uint32_t matchtag);
uint32_t flux_matchtag_avail (flux_t *h, int flags);

/* Send a message
 * flags may be 0 or FLUX_O_TRACE or FLUX_O_NONBLOCK (FLUX_O_COPROC is ignored)
 * Returns 0 on success, -1 on failure with errno set.
 */
int flux_send (flux_t *h, const flux_msg_t *msg, int flags);

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
 * Returns fd on sucess, -1 on failure with errno set.
 */
int flux_pollfd (flux_t *h);

/* Event subscribe/unsubscribe.
 */
int flux_event_subscribe (flux_t *h, const char *topic);
int flux_event_unsubscribe (flux_t *h, const char *topic);

/* Get/clear handle message counters.
 */
void flux_get_msgcounters (flux_t *h, flux_msgcounters_t *mcs);
void flux_clr_msgcounters (flux_t *h);

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
