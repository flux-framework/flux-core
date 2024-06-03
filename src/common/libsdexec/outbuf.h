/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_OUTBUF_H
#define _LIBSDEXEC_OUTBUF_H

#include <stdbool.h>
#include <sys/types.h>

/* The outbuf container was purpose-built for sdexec/channel.c.
 *
 * Putting data in the buffer:
 * - write up to outbuf_free() bytes to the location returned by outbuf_head()
 * - account for that with outbuf_mark_used().
 *
 * Taking data out of the buffer:
 * - read up to outbuf_used() bytes from the location returned by outbuf_tail()
 * - account for that with outbuf_mark_free().
 *
 * Call outbuf_gc() when done consuming data from the buffer.
 */
struct outbuf *outbuf_create (size_t size);
void outbuf_destroy (struct outbuf *buf);

char *outbuf_head (struct outbuf *buf);
size_t outbuf_free (struct outbuf *buf);
void outbuf_mark_used (struct outbuf *buf, size_t count);

// full in the sense that the entire buffer is used, even after gc
bool outbuf_full (struct outbuf *buf);

char *outbuf_tail (struct outbuf *buf);
size_t outbuf_used (struct outbuf *buf);
void outbuf_mark_free (struct outbuf *buf, size_t count);
void outbuf_gc (struct outbuf *buf);

#endif /* !_LIBSDEXEC_OUTBUF_H */

// vi:ts=4 sw=4 expandtab
