/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <zmq.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "sockopt.h"
#include "mpart.h"

static void part_destroy (zmq_msg_t *part)
{
    if (part) {
        int saved_errno = errno;
        zmq_msg_close (part);
        free (part);
        errno = saved_errno;
    }
}

static zmq_msg_t *part_create (const void *data, size_t size)
{
    zmq_msg_t *part;

    if (!(part = calloc (1, sizeof (*part))))
        return NULL;
    if (size > 0) {
        if (zmq_msg_init_size (part, size) < 0)
            goto error;
        if (data)
            memcpy (zmq_msg_data (part), data, size);
    }
    else
        (void)zmq_msg_init (part); // documented as always returning 0
    return part;
error:
    part_destroy (part);
    return NULL;
}

static zmq_msg_t *part_recv (void *sock)
{
    zmq_msg_t *part;

    if (!(part = part_create (NULL, 0)))
        return NULL;
    if (zmq_msg_recv (part, sock, 0) < 0) {
        part_destroy (part);
        return NULL;
    }
    return part;
}

static bool part_streq (zmq_msg_t *part, const char *s)
{
    if (part
        && s
        && zmq_msg_size (part) == strlen (s)
        && memcmp (zmq_msg_data (part), s, zmq_msg_size (part)) == 0)
        return true;
    return false;
}

void mpart_destroy (zlist_t *mpart)
{
    if (mpart) {
        int saved_errno = errno;
        zlist_destroy (&mpart);
        errno = saved_errno;
    }
}

zlist_t *mpart_create (void)
{
    zlist_t *mpart;

    if (!(mpart = zlist_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    return mpart;
}

static int mpart_append (zlist_t *mpart, zmq_msg_t *part)
{
    if (zlist_append (mpart, part) < 0) {
        errno = ENOMEM;
        return -1;
    }
    zlist_freefn (mpart, part, (zlist_free_fn *)part_destroy, true);
    return 0;
}

int mpart_addmem (zlist_t *mpart, const void *buf, size_t size)
{
    zmq_msg_t *part;

    if (!mpart) {
        errno = EINVAL;
        return -1;
    }
    if (!(part = part_create (buf, size)))
        return -1;
    if (mpart_append (mpart, part) < 0) {
        part_destroy (part);
        return -1;
    }
    return 0;
}

int mpart_addstr (zlist_t *mpart, const char *s)
{
    if (!mpart || !s) {
        errno = EINVAL;
        return -1;
    }
    return mpart_addmem (mpart, s, strlen (s));
}

zlist_t *mpart_recv (void *sock)
{
    zlist_t *mpart;
    int more;

    if (!(mpart = mpart_create ()))
        return NULL;
    do {
        zmq_msg_t *part;
        if (!(part = part_recv (sock)))
            goto error;
        if (mpart_append (mpart, part) < 0) {
            part_destroy (part);
            goto error;
        }
        if (zgetsockopt_int (sock, ZMQ_RCVMORE, &more) < 0)
            goto error;
    } while (more);
    return mpart;
error:
    mpart_destroy (mpart);
    return NULL;
}

int mpart_send (void *sock, zlist_t *mpart)
{
    if (!mpart) {
        errno = EINVAL;
        return -1;
    }
    int count = 0;
    int parts = zlist_size (mpart);
    zmq_msg_t *part = zlist_first (mpart);
    while (part) {
        int flags = 0;
        if (++count < parts)
            flags |= ZMQ_SNDMORE;
        if (zmq_msg_send (part, sock, flags) < 0)
            return -1;
        part = zlist_next (mpart);
    }
    return 0;
}

zmq_msg_t *mpart_get (zlist_t *mpart, int index)
{
    if (mpart) {
        zmq_msg_t *part;
        int count = 0;

        part = zlist_first (mpart);
        while (part) {
            if (count++ == index)
                return part;
            part = zlist_next (mpart);
        }
    }
    return NULL;
}

bool mpart_streq (zlist_t *mpart, int index, const char *s)
{
    if (mpart && s) {
        zmq_msg_t *part = mpart_get (mpart, index);
        if (part && part_streq (part, s))
            return true;
    }
    return false;
}

// vi:ts=4 sw=4 expandtab
