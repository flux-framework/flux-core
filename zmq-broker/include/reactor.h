#ifndef FLUX_REACTOR_H
#define FLUX_REACTOR_H

/* FluxMsgHandler indicates zmsg is "consumed" by destroy it.
 * Callbacks return 0 on success, -1 on error and set errno.
 * Error terminates reactor, and flux_reactor_start() returns -1.
 */
typedef int (*FluxMsgHandler)(flux_t h, int typemask, zmsg_t **zmsg, void *arg);
typedef int (*FluxFdHandler)(flux_t h, int fd, short revents, void *arg);
typedef int (*FluxZsHandler)(flux_t h, void *zs, short revents, void *arg);
typedef int (*FluxTmoutHandler)(flux_t h, void *arg);

/* Register a FluxMsgHandler callback to be called whenever a message
 * matching typemask and pattern (glob) is received.  The callback is
 * added to the beginning of the msghandler list.  If the callback
 * destroys the message, it is "consumed"; otherwise it falls through to
 * the next possible match.
 */
int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg);

/* Register a FluxMsgHandler callback as above, except the callback is
 * added to the end of the msghandler list.
 */
int flux_msghandler_append (flux_t h, int typemask, const char *pattern,
                            FluxMsgHandler cb, void *arg);

/* Unregister a FluxMsgHandler callback.  Only the first callback with
 * identical typemask and pattern is removed.
 */
void flux_msghandler_remove (flux_t h, int typemask, const char *pattern);


/* Register a FluxFdHandler callback to be called whenever an event
 * in the 'events' mask occurs on the given file descriptor 'fd'.
 */
int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg);

/* Unregister a FluxFdHandler callback.  Only the first callback with
 * identical fd and events is removed.
 */
void flux_fdhandler_remove (flux_t h, int fd, short events);


/* Register a FluxZsHandler callback to be called whenever an event
 * in the 'events' mask occurs on the given zeromq socket 'zs'.
 */
int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg);

/* Unregister a FluxZsHandler callback.  Only the first callback with
 * identical zs and events is removed.
 */
void flux_zshandler_remove (flux_t h, void *zs, short events);

/* Register a FluxTmoutHandler callback.  There can be only one.
 * This function internally calls flux_tmouthandler_remove() first
 * to replace existing callback, if any, and disarm timer.
 */
int flux_tmouthandler_set (flux_t h, FluxTmoutHandler cb, void *arg);

/* Unregister a FluxTmoutHandler callback and disarm timer.
 */
void flux_tmouthandler_remove (flux_t h);

/* Arm the reactor timer such that a FluxTmoutHandler,
 * if registered, will be called every 'msec' milliseconds.
 */
int flux_timeout_set (flux_t h, unsigned long msec);

/* Disarm the reactor timer.
 */
int flux_timeout_clear (flux_t h);

/* Test whether the reactor timer is armed.
 */
bool flux_timeout_isset (flux_t h);

/* Start the flux event reactor.
 * Returns 0 if flux_reactor_stop() terminated reactor; -1 if error did.
 */
int flux_reactor_start (flux_t h);

/* Signal that the flux event reactor should stop.
 * This may be called from within a FluxMsgHandler/FluxFdHandler callback.
 */
void flux_reactor_stop (flux_t h);

#endif /* !FLUX_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
