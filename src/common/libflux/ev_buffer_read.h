/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _EV_BUFFER_READ_H
#define _EV_BUFFER_READ_H

#include "src/common/libev/ev.h"
#include "src/common/libflux/buffer.h"

struct ev_buffer_read;

typedef void (*ev_buffer_read_f) (struct ev_loop *loop,
                                  struct ev_buffer_read *ebr,
                                  int revents);

struct ev_buffer_read {
    ev_io io_w;
    ev_prepare prepare_w;
    ev_idle idle_w;
    ev_check check_w;
    int fd;
    ev_buffer_read_f cb;
    flux_buffer_t *fb;
    struct ev_loop *loop;
    bool start;    /* flag, if user started reactor */
    bool eof_read; /* flag, if EOF on stream seen */
    bool eof_sent; /* flag, if EOF to user sent */
    bool line;     /* flag, if line buffered */
    void *data;
};

int ev_buffer_read_init (struct ev_buffer_read *ebr,
                         int fd,
                         int size,
                         ev_buffer_read_f cb,
                         struct ev_loop *loop);
void ev_buffer_read_cleanup (struct ev_buffer_read *ebr);
void ev_buffer_read_start (struct ev_loop *loop, struct ev_buffer_read *ebr);
void ev_buffer_read_stop (struct ev_loop *loop, struct ev_buffer_read *ebr);

#endif /* !_EV_BUFFER_READ_H */
