/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* msg_dequeue.c - reactive, thread-safe, output-restricted message deque */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <flux/core.h>

#include "ccan/list/list.h"

#include "message_private.h" // for access to msg->list
#include "msg_deque.h"

struct msg_deque {
    struct list_head messages;
    int pollevents;
    int pollfd;
    uint64_t event;
    pthread_mutex_t lock;
    int flags;
    int count;
    int limit; // 0 = unlimited
};

int msg_deque_get_limit (struct msg_deque *q)
{
    if (!q) {
        errno = EINVAL;
        return -1;
    }
    return q->limit;
}

int msg_deque_set_limit (struct msg_deque *q, int limit)
{
    if (!q || limit < 0) {
        errno = EINVAL;
        return -1;
    }
    q->limit = limit;
    return 0;
}

void msg_deque_destroy (struct msg_deque *q)
{
    if (q) {
        int saved_errno = errno;
        flux_msg_t *msg;
        while ((msg = msg_deque_pop_front (q)))
            flux_msg_destroy (msg);
        if (q->pollfd >= 0)
            (void)close (q->pollfd);
        if (!(q->flags & MSG_DEQUE_SINGLE_THREAD))
            pthread_mutex_destroy (&q->lock);
        free (q);
        errno = saved_errno;
    };
}

struct msg_deque *msg_deque_create (int flags)
{
    struct msg_deque *q;

    if (flags != 0 && flags != MSG_DEQUE_SINGLE_THREAD) {
        errno = EINVAL;
        return NULL;
    }
    if (!(q = calloc (1, sizeof (*q))))
        return NULL;
    q->flags = flags;
    q->pollfd = -1;
    q->pollevents = POLLOUT;
    if (!(flags & MSG_DEQUE_SINGLE_THREAD))
        pthread_mutex_init (&q->lock, NULL);
    list_head_init (&q->messages);
    return q;
}

static inline void msg_deque_lock (struct msg_deque *q)
{
    if (!(q->flags & MSG_DEQUE_SINGLE_THREAD))
        pthread_mutex_lock (&q->lock);
}

static inline void msg_deque_unlock (struct msg_deque *q)
{
    if (!(q->flags & MSG_DEQUE_SINGLE_THREAD))
        pthread_mutex_unlock (&q->lock);
}

// See eventfd(2) for an explanation of how signaling on q->pollfd works
static int msg_deque_raise_event (struct msg_deque *q)
{
    if (q->pollfd >= 0 && q->event == 0) {
        q->event = 1;
        if (write (q->pollfd, &q->event, sizeof (q->event)) < 0)
            return -1;
    }
    return 0;
}

static int msg_deque_clear_event (struct msg_deque *q)
{
    if (q->pollfd >= 0 && q->event == 1) {
        if (read (q->pollfd, &q->event, sizeof (q->event)) < 0) {
            if (errno != EAGAIN  && errno != EWOULDBLOCK)
                return -1;
            errno = 0;
        }
        q->event = 0;
    }
    return 0;
}

bool check_push_args (struct msg_deque *q, flux_msg_t *msg)
{
    if (!q || !msg)
        return false;
    /* When deque is used as a transport between threads, retaining a
     * reference on a message after pushing it might result in both threads
     * modifying the message simultaneously.  Therefore reject the operation
     * if references other than the one being transferred are held.
     */
    if (!(q->flags & MSG_DEQUE_SINGLE_THREAD) && msg->refcount > 1)
        return false;
    /* A message can only be in one msg_deque at a time.  Reject the operation
     * if the list node is not in the state left by list_node_init() and
     * list_del_init(), which is n.next == n.prev == n.
     */
    if (msg->list.next != &msg->list || msg->list.prev != &msg->list)
        return false;

    return true;
}

// call under lock
static inline bool msg_deque_full_at (struct msg_deque *q, int delta)
{
    if (q->limit != 0 && q->count + delta >= q->limit)
        return true;
    return false;
}

// call under lock
static inline bool msg_deque_empty_at (struct msg_deque *q, int delta)
{
    if (q->count + delta == 0)
        return true;
    return false;
}

int msg_deque_push_back (struct msg_deque *q, flux_msg_t *msg)
{
    if (!check_push_args (q, msg)) {
        errno = EINVAL;
        return -1;
    }
    msg_deque_lock (q);
    if (msg_deque_full_at (q, 0)) {
        errno = EWOULDBLOCK;
        goto error_unlock;
    }
    if (!(q->pollevents & POLLIN)) {
        q->pollevents |= POLLIN;
        if (msg_deque_raise_event (q) < 0)
            goto error_unlock;
    }
    if ((q->pollevents & POLLOUT) && msg_deque_full_at (q, +1))
        q->pollevents &= ~POLLOUT;
    list_add (&q->messages, &msg->list);
    q->count++;
    msg_deque_unlock (q);
    return 0;
error_unlock:
    msg_deque_unlock (q);
    return -1;
}

int msg_deque_push_front (struct msg_deque *q, flux_msg_t *msg)
{
    if (!check_push_args (q, msg)) {
        errno = EINVAL;
        return -1;
    }
    msg_deque_lock (q);
    if (msg_deque_full_at (q, 0)) {
        errno = EWOULDBLOCK;
        goto error_unlock;
    }
    if (!(q->pollevents & POLLIN)) {
        q->pollevents |= POLLIN;
        if (msg_deque_raise_event (q) < 0)
            goto error_unlock;
    }
    if ((q->pollevents & POLLOUT) && msg_deque_full_at (q, +1))
        q->pollevents &= ~POLLOUT;
    list_add_tail (&q->messages, &msg->list);
    q->count++;
    msg_deque_unlock (q);
    return 0;
error_unlock:
    msg_deque_unlock (q);
    return -1;
}

flux_msg_t *msg_deque_pop_front (struct msg_deque *q)
{
    if (!q)
        return NULL;
    msg_deque_lock (q);
    flux_msg_t *msg = list_tail (&q->messages, struct flux_msg, list);
    if (msg) {
        if (!(q->pollevents & POLLOUT) && !msg_deque_full_at (q, -1)) {
            q->pollevents |= POLLOUT;
            if (msg_deque_raise_event (q) < 0)
                goto error_unlock;
        }
        if ((q->pollevents & POLLIN) && msg_deque_empty_at (q, -1))
            q->pollevents &= ~POLLIN;
        list_del_init (&msg->list);
        q->count--;
    }
    msg_deque_unlock (q);
    return msg;
error_unlock:
    msg_deque_unlock (q);
    return NULL;
}

bool msg_deque_empty (struct msg_deque *q)
{
    if (!q)
        return true;
    msg_deque_lock (q);
    size_t count = q->count;
    msg_deque_unlock (q);
    return count == 0 ? true : false;
}

size_t msg_deque_count (struct msg_deque *q)
{
    if (!q)
        return 0;
    msg_deque_lock (q);
    size_t count = q->count;
    msg_deque_unlock (q);
    return count;
}

int msg_deque_pollfd (struct msg_deque *q)
{
    if (!q) {
        errno = EINVAL;
        return -1;
    }
    int rc;
    msg_deque_lock (q);
    if (q->pollfd < 0) {
        q->event = q->pollevents ? 1 : 0;
        q->pollfd = eventfd (q->event, EFD_NONBLOCK);
    }
    rc = q->pollfd;
    msg_deque_unlock (q);
    return rc;
}

int msg_deque_pollevents (struct msg_deque *q)
{
    if (!q) {
        errno = EINVAL;
        return -1;
    }
    int rc = -1;
    msg_deque_lock (q);
    if (msg_deque_clear_event (q) < 0)
        goto done;
    rc = q->pollevents;
done:
    msg_deque_unlock (q);
    return rc;
}

// vi:ts=4 sw=4 expandtab
