#ifndef _FLUX_CORE_DISPATCH_H
#define _FLUX_CORE_DISPATCH_H

#include "message.h"
#include "handle.h"


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

typedef void (*flux_msg_watcher_f)(flux_t h, flux_watcher_t *w,
                                   const flux_msg_t *msg, void *arg);

flux_watcher_t *flux_msg_watcher_create (struct flux_match match,
                                         flux_msg_watcher_f cb, void *arg);

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
    flux_watcher_t *w;
};
#define FLUX_MSGHANDLER_TABLE_END { 0, NULL, NULL }

int flux_msg_watcher_addvec (flux_t h, struct flux_msghandler tab[], void *arg);
void flux_msg_watcher_delvec (flux_t h, struct flux_msghandler tab[]);

/* Give control back to the reactor until a message matching 'match'
 * is queued in the handle.  This will return -1 with errno = EINVAL
 * if called from a reactor handler that is not running in as a coprocess.
 * Currently only message handlers are started as coprocesses, if the
 * handle has FLUX_O_COPROC set.  This is used internally by flux_recv().
 */
int flux_sleep_on (flux_t h, struct flux_match match);

#endif /* !_FLUX_CORE_DISPATCH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
