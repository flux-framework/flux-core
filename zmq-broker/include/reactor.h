#ifndef REACTOR_H
#define REACTOR_H

typedef void (*FluxMsgHandler)(flux_t h, int typemask, zmsg_t **zmsg, void *arg);
typedef void (*FluxFdHandler)(flux_t h, int fd, short revents, void *arg);

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg);
int flux_msghandler_append (flux_t h, int typemask, const char *pattern,
                            FluxMsgHandler cb, void *arg);
void flux_msghandler_remove (flux_t h, int typemask, const char *pattern);

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg);
void flux_fdhandler_remove (flux_t h, int fd, short events);

int flux_reactor_start (flux_t h);
void flux_reactor_stop (flux_t h);

#endif /* !defined(REACTOR_H) */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
