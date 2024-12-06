/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZMQUTIL_MPART_H
#define _ZMQUTIL_MPART_H

#include <stdbool.h>
#include <zmq.h>
#include "src/common/libczmqcontainers/czmq_containers.h"

/* helpers for multi-part messages as zlist of zmq_msg_t
 * (like stripped down zmsg_t)
 */
void mpart_destroy (zlist_t *mpart);
zlist_t *mpart_create (void);
int mpart_addmem (zlist_t *mpart, const void *buf, size_t size);
int mpart_addstr (zlist_t *mpart, const char *s);
zlist_t *mpart_recv (void *sock);
int mpart_send (void *sock, zlist_t *mpart);
zmq_msg_t *mpart_get (zlist_t *mpart, int index);
bool mpart_streq (zlist_t *mpart, int index, const char *s);

#endif // !_ZMQUTIL_MPART_H

// vi:ts=4 sw=4 expandtab
