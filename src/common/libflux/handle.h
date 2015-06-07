#ifndef _FLUX_CORE_HANDLE_H
#define _FLUX_CORE_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "message.h"

struct _zctx_t;

typedef struct flux_handle_struct *flux_t;

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

/* Create/destroy a broker handle.
 * The 'uri' scheme name selects a handle implementation to dynamically load.
 * The rest of the URI is parsed in an implementation-specific manner.
 * A NULL uri selects the "local" implementation with path set to the value
 * of FLUX_TMPDIR.
 */
flux_t flux_open (const char *uri, int flags);
void flux_close (flux_t h);

/* Register a handler for fatal handle errors.
 * A fatal error is ENOMEM or a handle send/recv error after which
 * it is inadvisable to continue using the handle.
 */
void flux_fatal_set (flux_t h, flux_fatal_f fun, void *arg);

/* Call the user's fatal error handler, if any.
 * If no handler is registered, this is a no-op.
 */
void flux_fatal_error (flux_t h, const char *fun, const char *msg);
#define FLUX_FATAL(h) do { \
    flux_fatal_error((h),__FUNCTION__,(strerror (errno))); \
} while (0)

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The flux_free_f, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 */
typedef void (*flux_free_f)(void *arg);
void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, flux_free_f destroy);

/* Set/clear FLUX_O_* on a flux_t handle.
 */
void flux_flags_set (flux_t h, int flags);
void flux_flags_unset (flux_t h, int flags);
int flux_flags_get (flux_t h);

/* Alloc/free a matchtag block for matched requests.
 */
uint32_t flux_matchtag_alloc (flux_t h, int size);
void flux_matchtag_free (flux_t h, uint32_t t, int size);
uint32_t flux_matchtag_avail (flux_t h);

/* Low level message send/recv functions.
 */
int flux_send (flux_t h, const flux_msg_t msg, int flags);
flux_msg_t flux_recv (flux_t h, flux_match_t match, int flags);

/* deprecated */
int flux_sendmsg (flux_t h, flux_msg_t *msg);
flux_msg_t flux_recvmsg (flux_t h, bool nonblock);
flux_msg_t flux_recvmsg_match (flux_t h, flux_match_t match, bool nonblock);

/* Requeue message in the handle (head or tail according to flags)
 */
int flux_requeue (flux_t h, const flux_msg_t msg, int flags);

/* Event subscribe/unsubscribe.
 */
int flux_event_subscribe (flux_t h, const char *topic);
int flux_event_unsubscribe (flux_t h, const char *topic);

/* Get handle's zctx (if any).
 */
struct _zctx_t *flux_get_zctx (flux_t h);

/* Get/clear handle message counters.
 */
void flux_get_msgcounters (flux_t h, flux_msgcounters_t *mcs);
void flux_clr_msgcounters (flux_t h);

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
