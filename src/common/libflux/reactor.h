#ifndef _FLUX_CORE_REACTOR_H
#define _FLUX_CORE_REACTOR_H

#include <stdbool.h>

#include "message.h"
#include "handle.h"

/* flags for flux_reactor_run ()
 */
enum {
    FLUX_REACTOR_NOWAIT = 1,  /* return after all new and outstanding */
                              /*     events have been hnadled */
    FLUX_REACTOR_ONCE = 2,    /* same as above but block until at least */
                              /*     one event occurs */
};

/* Start the flux event reactor.
 * Returns 0 if flux_reactor_stop() terminated reactor; -1 if error did.
 */
int flux_reactor_start (flux_t h);

/* Start the flux event reactor, with optional flags.
 * Returns 0 if flux_reactor_stop() terminated reactor; -1 if error did.
 */
int flux_reactor_run (flux_t h, int flags);

/* Signal that the flux event reactor should stop.
 * This may be called from within a watcher.
 */
void flux_reactor_stop (flux_t h);
void flux_reactor_stop_error (flux_t h);

/* Give control back to the reactor until a message matching 'match'
 * is queued in the handle.  This will return -1 with errno = EINVAL
 * if called from a reactor handler that is not running in as a coprocess.
 * Currently only message handlers are started as coprocesses, if the
 * handle has FLUX_O_COPROC set.  This is used internally by flux_recv().
 */
int flux_sleep_on (flux_t h, struct flux_match match);

/* General comments on watchers:
 * - It is safe to call a watcher's 'stop' function from a watcher callback.
 * - It is necessary to stop a watcher before destroying it.
 * - Once stopped, a watcher is no longer associated with the flux handle
 *   and must be destroyed independently.
 * - A watcher may be started/stopped more than once.
 * - Starting a started watcher, stopping a stopped watcher, or destroying
 *   a NULL watcher have no effect.
 */

/* Message dispatch
 * Create/destroy/start/stop "message watchers".
 * A message watcher handles messages received on the handle matching 'match'.
 * Message watchers are special compared to the other watchers as they
 * combine an internal "handle watcher" that reads new messages from the
 * handle as they arrive, and a dispatcher that hands the message to a
 * matching message watcher.
 *
 * If multiple message watchers match a given message, the most recently
 * registered will handle it.  Thus it is possible to register handlers for
 * "svc.*" then "svc.foo", and the former will match all methods but "foo".
 * If a request message arrives that is not matched by a message watcher,
 * the reactor sends a courtesy ENOSYS response.
 *
 * If the handle was created with FLUX_O_COPROC, message watchers will be
 * run in a coprocess context.  If they make an RPC call or otherwise call
 * flux_recv(), the reactor can run, handling other tasks until the desired
 * message arrives, then the message watcher is restarted.
 * Currently only message watchers run as coprocesses.
 */

typedef struct flux_msg_watcher flux_msg_watcher_t;
typedef void (*flux_msg_watcher_f)(flux_t h, flux_msg_watcher_t *w,
                                   const flux_msg_t *msg, void *arg);

flux_msg_watcher_t *flux_msg_watcher_create (struct flux_match match,
                                             flux_msg_watcher_f cb, void *arg);
void flux_msg_watcher_destroy (flux_msg_watcher_t *w);
void flux_msg_watcher_start (flux_t h, flux_msg_watcher_t *w);
void flux_msg_watcher_stop (flux_t h, flux_msg_watcher_t *w);

/* Convenience functions for bulk add/remove of message watchers.
 * addvec creates/adds a table of message watchers
 * (created watchers are then stored in the table)
 * delvec stops/destroys the table of message watchers.
 * addvec returns 0 on success, -1 on failure with errno set.
 * Watchers are added beginning with tab[0] (see multiple match comment above).
 * tab[] must be terminated with FLUX_MSGHANDLER_TABLE_END.
 */

struct flux_msghandler {
    int typemask;
    char *topic_glob;
    flux_msg_watcher_f cb;
    flux_msg_watcher_t *w;
};
#define FLUX_MSGHANDLER_TABLE_END { 0, NULL, NULL }
int flux_msg_watcher_addvec (flux_t h, struct flux_msghandler tab[], void *arg);
void flux_msg_watcher_delvec (flux_t h, struct flux_msghandler tab[]);

/* file descriptors
 * Create/destroy/start/stop "fd watchers".
 * A file descriptor watcher monitors a file descriptor for FLUX_POLLIN
 * and/or FLUX_POLLOUT as specified in the events paramter.  The watcher
 * is called with the events that triggered the watcher set in revents.
 * If there was a poll or other error on the fd, revents may be called with
 * FLUX_POLLERR set.
 */

typedef struct flux_fd_watcher flux_fd_watcher_t;
typedef void (*flux_fd_watcher_f)(flux_t h, flux_fd_watcher_t *w,
                                  int fd, int revents, void *arg);
flux_fd_watcher_t *flux_fd_watcher_create (int fd, int events,
                                           flux_fd_watcher_f cb, void *arg);
void flux_fd_watcher_destroy (flux_fd_watcher_t *w);
void flux_fd_watcher_start (flux_t h, flux_fd_watcher_t *w);
void flux_fd_watcher_stop (flux_t h, flux_fd_watcher_t *w);

/* 0MQ sockets
 * Create/destroy/start/stop "zmq watchers".
 * zmq watchers behave exactly like fd watchers.
 */

typedef struct flux_zmq_watcher flux_zmq_watcher_t;
typedef void (*flux_zmq_watcher_f)(flux_t h, flux_zmq_watcher_t *w,
                                   void *zsock, int revents, void *arg);
flux_zmq_watcher_t *flux_zmq_watcher_create (void *zsock, int events,
                                             flux_zmq_watcher_f cb, void *arg);
void flux_zmq_watcher_destroy (flux_zmq_watcher_t *w);
void flux_zmq_watcher_start (flux_t h, flux_zmq_watcher_t *w);
void flux_zmq_watcher_stop (flux_t h, flux_zmq_watcher_t *w);

/* Timer
 * Create/destroy/start/stop "timer watchers".
 * A timer is set to trigger 'after' seconds from its start time.  If 'after'
 * is zero, the timer triggers immediately.  If 'repeat' is zero, the timer
 * only triggers once and is then automatically stopped, otherwise it triggers
 * again every 'repeat' seconds.
 */

typedef struct flux_timer_watcher flux_timer_watcher_t;
typedef void (*flux_timer_watcher_f)(flux_t h, flux_timer_watcher_t *w,
                                     int revents, void *arg);
flux_timer_watcher_t *flux_timer_watcher_create (double after, double repeat,
                                                 flux_timer_watcher_f cb,
                                                 void *arg);
void flux_timer_watcher_destroy (flux_timer_watcher_t *w);
void flux_timer_watcher_start (flux_t h, flux_timer_watcher_t *w);
void flux_timer_watcher_stop (flux_t h, flux_timer_watcher_t *w);
void flux_timer_watcher_reset (flux_timer_watcher_t *w,
                               double after, double repeat);


/* Prepare - run immediately before blocking.
 */
typedef struct flux_prepare_watcher flux_prepare_watcher_t;
typedef void (*flux_prepare_watcher_f)(flux_t h, flux_prepare_watcher_t *w,
                                     int revents, void *arg);
flux_prepare_watcher_t *flux_prepare_watcher_create (flux_prepare_watcher_f cb,
                                                     void *arg);
void flux_prepare_watcher_destroy (flux_prepare_watcher_t *w);
void flux_prepare_watcher_start (flux_t h, flux_prepare_watcher_t *w);
void flux_prepare_watcher_stop (flux_t h, flux_prepare_watcher_t *w);

/* Check - run immediately after blocking
 */
typedef struct flux_check_watcher flux_check_watcher_t;
typedef void (*flux_check_watcher_f)(flux_t h, flux_check_watcher_t *w,
                                     int revents, void *arg);
flux_check_watcher_t *flux_check_watcher_create (flux_check_watcher_f cb,
                                                     void *arg);
void flux_check_watcher_destroy (flux_check_watcher_t *w);
void flux_check_watcher_start (flux_t h, flux_check_watcher_t *w);
void flux_check_watcher_stop (flux_t h, flux_check_watcher_t *w);

/* Idle - always run (event loop never blocks)
 */
typedef struct flux_idle_watcher flux_idle_watcher_t;
typedef void (*flux_idle_watcher_f)(flux_t h, flux_idle_watcher_t *w,
                                     int revents, void *arg);
flux_idle_watcher_t *flux_idle_watcher_create (flux_idle_watcher_f cb,
                                                     void *arg);
void flux_idle_watcher_destroy (flux_idle_watcher_t *w);
void flux_idle_watcher_start (flux_t h, flux_idle_watcher_t *w);
void flux_idle_watcher_stop (flux_t h, flux_idle_watcher_t *w);

#endif /* !_FLUX_CORE_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
