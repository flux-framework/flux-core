#ifndef REACTOR_H
#define REACTOR_H

typedef void (*FluxMsgHandler)(flux_t h, int typemask, zmsg_t **zmsg, void*arg);
typedef void (*FluxFdHandler)(flux_t h, int fd, short revents, void *arg);
typedef void (*FluxZsHandler)(flux_t h, void *zs, short revents, void *arg);

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


/* Regisiter a FluxFdHandler callback to be called whenever an event
 * in the 'events' mask occurs on the given file descriptor 'fd'.
 */
int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg);

/* Unregister a FluxFdHandler callback.  Only the first callback with
 * identical fd and events is removed.
 */
void flux_fdhandler_remove (flux_t h, int fd, short events);


/* Regisiter a FluxZsHandler callback to be called whenever an event
 * in the 'events' mask occurs on the given zeromq socket 'zs'.
 */
int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg);

/* Unregister a FluxZsHandler callback.  Only the first callback with
 * identical zs and events is removed.
 */
void flux_zshandler_remove (flux_t h, void *zs, short events);

/* Start the flux event reactor.
 */
int flux_reactor_start (flux_t h);

/* Signal that the flux event reactor should stop.
 * This may be called from within a FluxMsgHandler/FluxFdHandler callback.
 */
void flux_reactor_stop (flux_t h);

#endif /* !defined(REACTOR_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
