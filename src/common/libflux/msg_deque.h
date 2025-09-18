/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MSG_DEQUE_H
#define _FLUX_CORE_MSG_DEQUE_H

#include <sys/types.h>

/* If flags contains MSG_DEQUE_SINGLE_THREAD, pthread locking is eliminated
 * and messages are permitted to be pushed with a reference count > 1.
 */
enum {
    MSG_DEQUE_SINGLE_THREAD = 1,
};

struct msg_deque *msg_deque_create (int flags);
void msg_deque_destroy (struct msg_deque *q);

/* By default, there are no limts on msg_deques.
 * If one is added, then upon reaching full, push fails with EWOULDBLOCK
 * and upon reaching non-full, POLLOUT is raised.
 * A limit of zero means unlimited.
 */
int msg_deque_set_limit (struct msg_deque *q, int limit);
int msg_deque_get_limit (struct msg_deque *q);

/* msg_deque_push_back() and msg_deque_push_front() steal a reference on
 * 'msg' on success.  If MSG_DEQUE_SINGLE_THREAD was not specified, then
 * that is expected to be the *only* reference and further access to the
 * message by the caller is not permitted.
 */
int msg_deque_push_back (struct msg_deque *q, flux_msg_t *msg);
int msg_deque_push_front (struct msg_deque *q, flux_msg_t *msg);
flux_msg_t *msg_deque_pop_front (struct msg_deque *q);

int msg_deque_pollfd (struct msg_deque *q);
int msg_deque_pollevents (struct msg_deque *q);

bool msg_deque_empty (struct msg_deque *q);
size_t msg_deque_count (struct msg_deque *q);

#endif // !_FLUX_CORE_MSG_DEQUE_H

// vi:ts=4 sw=4 expandtab
