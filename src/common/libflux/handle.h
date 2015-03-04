#ifndef _FLUX_CORE_HANDLE_H
#define _FLUX_CORE_HANDLE_H

#include <stdint.h>
#include <stdbool.h>
#include <czmq.h>
#include "message.h"

typedef struct flux_handle_struct *flux_t;

/* Flags for handle creation and flux_flags_set()/flux_flags_unset.
 */
enum {
    FLUX_FLAGS_TRACE = 1,   /* print 0MQ messages sent over the flux_t */
                            /*   handle on stdout. */
};

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The FluxFreeFn, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 */
typedef void (*FluxFreeFn)(void *arg);
void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy);

/* Set/clear FLUX_FLAGS_* on a flux_t handle.
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
int flux_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_recvmsg (flux_t h, bool nonblock);
int flux_putmsg (flux_t h, zmsg_t **zmsg);

/* Receive a message matching 'match' (see message.h).
 * Any unmatched messages are returned to the handle with flux_putmsg(),
 * unless 'nomatch' is non-NULL, in which case they are appended to the
 * list pointed to by 'nomatch' for you to deal with.
 */
zmsg_t *flux_recvmsg_match (flux_t h, flux_match_t match, zlist_t *nomatch,
                            bool nonblock);

/* Pop messages off 'list' and call flux_putmsg() on them.
 * If there were any errors, -1 is returned with the greatest errno set.
 * The list is always returned empty. 
 */
int flux_putmsg_list (flux_t h, zlist_t *list);

/* Get handle's zctx (if any).
 */
zctx_t *flux_get_zctx (flux_t h);

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
