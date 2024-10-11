/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSUBPROCESS_EV_FBUF_WRITE_H
#define _LIBSUBPROCESS_EV_FBUF_WRITE_H

#include "src/common/libev/ev.h"
#include "fbuf.h"

struct ev_fbuf_write;

typedef void (*ev_fbuf_write_f)(struct ev_loop *loop,
                                struct ev_fbuf_write *ebw,
                                int revents);

struct ev_fbuf_write {
    ev_io             io_w;
    int               fd;
    ev_fbuf_write_f   cb;
    struct fbuf       *fb;
    struct ev_loop    *loop;
    bool              start;    /* flag, if user started reactor */
    bool              eof;      /* flag, eof written             */
    bool              closed;   /* flag, fd has been closed      */
    int               close_errno;  /* errno from close          */
    bool              initial_space; /* flag, initial space notified */
    void              *data;
};

int ev_fbuf_write_init (struct ev_fbuf_write *ebw,
                        int fd,
                        int size,
                        ev_fbuf_write_f cb,
                        struct ev_loop *loop);
void ev_fbuf_write_cleanup (struct ev_fbuf_write *ebw);
void ev_fbuf_write_start (struct ev_loop *loop, struct ev_fbuf_write *ebw);
void ev_fbuf_write_stop (struct ev_loop *loop, struct ev_fbuf_write *ebw);
void ev_fbuf_write_wakeup (struct ev_fbuf_write *ebw);
#endif /* !_EV_BUFFER_WRITE_H */

// vi: ts=4 sw=4 expandtab
