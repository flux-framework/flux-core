#ifndef _FLUX_CORE_REACTOR_H
#define _FLUX_CORE_REACTOR_H

#include <stdbool.h>

#include "message.h"
#include "handle.h"

/* Start the flux event reactor.
 * Returns 0 if flux_reactor_stop() terminated reactor; -1 if error did.
 */
int flux_reactor_start (flux_t h);

/* Signal that the flux event reactor should stop.
 * This may be called from within a FluxMsgHandler/FluxFdHandler callback.
 */
void flux_reactor_stop (flux_t h);
void flux_reactor_stop_error (flux_t h);

/* Give control back to the reactor until a message matching 'match'
 * is queued in the handle.  This will return -1 with errno = EINVAL
 * if called from a reactor handler that is not running in as a coprocess.
 * Currently only message handlers are started as coprocesses, if the
 * handle has FLUX_O_COPROC set.
 */
int flux_sleep_on (flux_t h, struct flux_match match);

/* Message dispatch
 */

typedef struct flux_msg_watcher flux_msg_watcher_t;
typedef void (*flux_msg_watcher_f)(flux_t h, flux_msg_watcher_t *w,
                                   const flux_msg_t *msg, void *arg);

flux_msg_watcher_t *flux_msg_watcher_create (struct flux_match match,
                                             flux_msg_watcher_f cb, void *arg);
void flux_msg_watcher_destroy (flux_msg_watcher_t *w);
void flux_msg_watcher_start (flux_t h, flux_msg_watcher_t *w);
void flux_msg_watcher_stop (flux_t h, flux_msg_watcher_t *w);

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


#endif /* !_FLUX_CORE_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
