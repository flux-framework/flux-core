#ifndef _FLUX_CORE_REACTOR_H
#define _FLUX_CORE_REACTOR_H

#include <stdbool.h>

#include "handle.h"

/* flags for flux_reactor_run ()
 */
enum {
    FLUX_REACTOR_NOWAIT = 1,  /* return after all new and outstanding */
                              /*     events have been hnadled */
    FLUX_REACTOR_ONCE = 2,    /* same as above but block until at least */
                              /*     one event occurs */
};

/* Start the flux event reactor, with optional flags.
 * Returns 0 if flux_reactor_stop() terminated reactor; -1 if error did.
 */
int flux_reactor_run (flux_t h, int flags);

/* Signal that the flux event reactor should stop.
 * This may be called from within a watcher.
 */
void flux_reactor_stop (flux_t h);
void flux_reactor_stop_error (flux_t h);

/* Arrange for 'h2' to use the the built-in reactor of 'h'.
 */
void flux_reactor_add (flux_t h, flux_t h2);

/* General comments on watchers:
 * - It is safe to call a watcher's 'stop' function from a watcher callback.
 * - It is necessary to stop a watcher before destroying it.
 * - Once stopped, a watcher is no longer associated with the flux handle
 *   and must be destroyed independently.
 * - A watcher may be started/stopped more than once.
 * - Starting a started watcher, stopping a stopped watcher, or destroying
 *   a NULL watcher have no effect.
 */

typedef struct flux_watcher flux_watcher_t;
typedef void (*flux_watcher_f)(flux_t h, flux_watcher_t *w,
                               int revents, void *arg);
void flux_watcher_destroy (flux_watcher_t *w);
void flux_watcher_start (flux_t h, flux_watcher_t *w);
void flux_watcher_stop (flux_t h, flux_watcher_t *w);

/* handle - watch a flux_t handle
 */
flux_watcher_t *flux_handle_watcher_create (flux_t h, int events,
                                            flux_watcher_f cb, void *arg);
flux_t flux_handle_watcher_get_flux (flux_watcher_t *w);

/* fd - watch a file descriptor
 */
flux_watcher_t *flux_fd_watcher_create (int fd, int events,
                                        flux_watcher_f cb, void *arg);
int flux_fd_watcher_get_fd (flux_watcher_t *w);

/* zmq - watch a zeromq socket.
 */
flux_watcher_t *flux_zmq_watcher_create (void *zsock, int events,
                                         flux_watcher_f cb, void *arg);
void *flux_zmq_watcher_get_zsock (flux_watcher_t *w);

/* Timer - set a timer
 * Create/destroy/start/stop "timer watchers".
 * A timer is set to trigger 'after' seconds from its start time.  If 'after'
 * is zero, the timer triggers immediately.  If 'repeat' is zero, the timer
 * only triggers once and is then automatically stopped, otherwise it triggers
 * again every 'repeat' seconds.
 */

flux_watcher_t *flux_timer_watcher_create (double after, double repeat,
                                           flux_watcher_f cb, void *arg);
void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat);


/* Prepare - run immediately before blocking.
 */
flux_watcher_t *flux_prepare_watcher_create (flux_watcher_f cb, void *arg);

/* Check - run immediately after blocking
 */
flux_watcher_t *flux_check_watcher_create (flux_watcher_f cb, void *arg);

/* Idle - always run (event loop never blocks)
 */
flux_watcher_t *flux_idle_watcher_create (flux_watcher_f cb, void *arg);


/* watcher construction set
 */
struct watcher_ops {
    void (*start)(void *impl, flux_t h, flux_watcher_t *w);
    void (*stop)(void *impl, flux_t h, flux_watcher_t *w);
    void (*destroy)(void *impl, flux_watcher_t *w);
};

flux_watcher_t *flux_watcher_create (void *impl, struct watcher_ops ops,
                                     int signature, flux_watcher_f callback,
                                     void *arg);

#endif /* !_FLUX_CORE_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
