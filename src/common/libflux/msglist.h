/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MSGLIST_H
#define _FLUX_CORE_MSGLIST_H

#ifdef __cplusplus
extern "C" {
#endif

/* message lists
 * - takes a ref on 'msg' upon append
 * - drops ref on 'msg' on delete / list destroy
 */
struct flux_msglist *flux_msglist_create (void);
void flux_msglist_destroy (struct flux_msglist *l);

int flux_msglist_push (struct flux_msglist *l, const flux_msg_t *msg);
int flux_msglist_append (struct flux_msglist *l, const flux_msg_t *msg);
void flux_msglist_delete (struct flux_msglist *l); // (at cursor) iteration safe
const flux_msg_t *flux_msglist_pop (struct flux_msglist *l);

const flux_msg_t *flux_msglist_first (struct flux_msglist *l);
const flux_msg_t *flux_msglist_next (struct flux_msglist *l);
const flux_msg_t *flux_msglist_last (struct flux_msglist *l);

int flux_msglist_count (struct flux_msglist *l);

/* These functions are for integration of flux_msglist into an event loop.
 * The pollfd file descriptor becomes readable when a poll event has been
 * raised (edge triggered).  This indicates that the pollevents mask has been
 * updated.  The mask cnosists of POLLIN | POLLOUT | POLLERR.  N.B. POLLOUT
 * is always ready in the current implementation.
 * Both functions return -1 on error with errno set.
 */
int flux_msglist_pollevents (struct flux_msglist *l);
int flux_msglist_pollfd (struct flux_msglist *l);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_MSGLIST_H */

/*
 * vi:ts=4 sw=4 expandtab
 */

