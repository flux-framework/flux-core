/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_EV_FBUF_READ_H
#define _LIBSUBPROCESS_EV_FBUF_READ_H

#include "src/common/libev/ev.h"
#include "fbuf.h"

struct ev_fbuf_read;

typedef void (*ev_fbuf_read_f)(struct ev_loop *loop,
                               struct ev_fbuf_read *ebr,
                               int revents);

struct ev_fbuf_read {
    int              refcnt;
    ev_io            io_w;
    ev_prepare       prepare_w;
    ev_idle          idle_w;
    ev_check         check_w;
    int              fd;
    ev_fbuf_read_f   cb;
    struct fbuf      *fb;
    struct ev_loop   *loop;
    bool             start;     /* flag, if user started reactor */
    bool             eof_read;  /* flag, if EOF on stream seen */
    bool             eof_sent;  /* flag, if EOF to user sent */
    bool             line;      /* flag, if line buffered */
    void             *data;
};

int ev_fbuf_read_init (struct ev_fbuf_read *ebr,
                       int fd,
                       int size,
                       ev_fbuf_read_f cb,
                       struct ev_loop *loop);
void ev_fbuf_read_cleanup (struct ev_fbuf_read *ebr);
void ev_fbuf_read_start (struct ev_loop *loop, struct ev_fbuf_read *ebr);
void ev_fbuf_read_stop (struct ev_loop *loop, struct ev_fbuf_read *ebr);
bool ev_fbuf_read_is_active (struct ev_fbuf_read *ebr);
void ev_fbuf_read_incref (struct ev_fbuf_read *ebr);
void ev_fbuf_read_decref (struct ev_fbuf_read *ebr);

#endif /* !_LIBSUBPROCESS_EV_FBUF_READ_H */

// vi: ts=4 sw=4 expandtab
