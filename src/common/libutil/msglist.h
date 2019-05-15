/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_MSGLIST_H
#define _UTIL_MSGLIST_H

#include <poll.h>

typedef struct msglist_struct msglist_t;

typedef void (*msglist_free_f) (void *item);

/* Create/destroy list.
 * If 'fun' is non-NULL, msglist_destroy () will use it to destroy any
 * items on the list at that time.
 */
msglist_t *msglist_create (msglist_free_f fun);
void msglist_destroy (msglist_t *l);

/* Add/remove items from the list.
 * Push/append returns 0 on success, -1 on error with errno set.
 */
void *msglist_pop (msglist_t *l);
int msglist_push (msglist_t *l, void *item);
int msglist_append (msglist_t *l, void *item);

/* Iteration, removal, count.
 */
void *msglist_first (msglist_t *l);
void *msglist_next (msglist_t *l);
void msglist_remove (msglist_t *l, void *item);
int msglist_count (msglist_t *l);

/* Get the list 'pollevents' bitmask.
 * POLLIN = items can be removed with msglist_pop()
 * POLLOUT = items can be added wtih msglist_push() / msglist_append().
 * POLLERR = the msglist code encountered an error (eventfd related or ENOMEM)
 * Returns pollevents on success, -1 on error with errno set.
 */
int msglist_pollevents (msglist_t *l);

/* Obtain a file descriptor that will be readable when one of the pollevents
 * bits has been raised (edge triggered).  This file descriptor belongs
 * to msglist_t and should not be operated on except to integrate msglist_t
 * into a poll/event loop.  Returns fd on success, -1 on error with errno set.
 */
int msglist_pollfd (msglist_t *l);

#endif /* !_UTIL_MSGLIST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
