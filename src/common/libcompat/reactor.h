/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_COMPAT_REACTOR_H
#define _FLUX_COMPAT_REACTOR_H

#include <flux/core.h>

#define flux_msghandler_add         compat_msghandler_add
#define flux_msghandler_remove      compat_msghandler_remove
#define flux_fdhandler_add          compat_fdhandler_add
#define flux_fdhandler_remove       compat_fdhandler_remove
#define flux_tmouthandler_add       compat_tmouthandler_add
#define flux_tmouthandler_remove    compat_tmouthandler_remove

/* FluxMsgHandler indicates msg is "consumed" by destroying it.
 * Callbacks return 0 on success, -1 on error and set errno.
 * Error terminates reactor, and flux_reactor_start() returns -1.
 */
typedef int (*FluxMsgHandler)(flux_t *h, int typemask, flux_msg_t **msg,
                              void *arg);
typedef int (*FluxFdHandler)(flux_t *h, int fd, short revents, void *arg);
typedef int (*FluxTmoutHandler)(flux_t *h, void *arg);

typedef struct {
    int typemask;
    const char *pattern;
    FluxMsgHandler cb;
} msghandler_t;

/* Register a FluxMsgHandler callback to be called whenever a message
 * matching typemask and pattern (glob) is received.  The callback is
 * added to the beginning of the msghandler list.
 */
int flux_msghandler_add (flux_t *h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
                         __attribute__ ((deprecated));

/* Unregister a FluxMsgHandler callback.  Only the first callback with
 * identical typemask and pattern is removed.
 */
void flux_msghandler_remove (flux_t *h, int typemask, const char *pattern)
                             __attribute__ ((deprecated));


/* Register a FluxFdHandler callback to be called whenever an event
 * in the 'events' mask occurs on the given file descriptor 'fd'.
 */
int flux_fdhandler_add (flux_t *h, int fd, short events,
                        FluxFdHandler cb, void *arg)
                        __attribute__ ((deprecated));

/* Unregister a FluxFdHandler callback.  Only the first callback with
 * identical fd and events is removed.
 */
void flux_fdhandler_remove (flux_t *h, int fd, short events)
                            __attribute__ ((deprecated));


/* Register a FluxTmoutHandler callback.  Returns timer_id or -1 on error.
 */
int flux_tmouthandler_add (flux_t *h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
                           __attribute__ ((deprecated));

/* Unregister a FluxTmoutHandler callback.
 */
void flux_tmouthandler_remove (flux_t *h, int timer_id)
                               __attribute__ ((deprecated));


#endif /* !_FLUX_COMPAT_REACTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
