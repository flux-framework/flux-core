/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* outbuf.c - output buffer for channel.c
 *
 * outbuf is a linear buffer which allows data to be removed in contiguous
 * chunks of our choosing (for example lines) without copying, unlike cbuf.
 * However, the buffer space has to be reclaimed after data has been taken
 * out by calling output_gc(). This works here because the channel_output_cb()
 * watcher aggressively flushes the buffer after putting data in it.  The gc
 * can be called just before the watcher returns.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "outbuf.h"

struct outbuf {
    char *data;
    size_t size;
    size_t offset;  // valid data begins at buf.data + buf.offset
    size_t used;    // bytes used starting at buf.data + buf.offset
};

char *outbuf_head (struct outbuf *buf)
{
    return buf->data + buf->offset + buf->used;
}

size_t outbuf_free (struct outbuf *buf)
{
    return buf->size - (buf->offset + buf->used);
}

void outbuf_mark_used (struct outbuf *buf, size_t count)
{
    buf->used += count;
}

// "full" in the sense that even after gc there will be no room for new data
bool outbuf_full (struct outbuf *buf)
{
    return (buf->size == buf->used) ? true : false;
}

char *outbuf_tail (struct outbuf *buf)
{
    return buf->data + buf->offset;
}

size_t outbuf_used (struct outbuf *buf)
{
    return buf->used;
}

void outbuf_mark_free (struct outbuf *buf, size_t count)
{
    buf->offset += count;
    buf->used -= count;
}

void outbuf_gc (struct outbuf *buf)
{
    if (buf->offset > 0) {
        memmove (buf->data, buf->data + buf->offset, buf->used);
        buf->offset = 0;
    }
}

struct outbuf *outbuf_create (size_t size)
{
    struct outbuf *buf;
    if (!(buf = calloc (1, sizeof (*buf) + size)))
        return NULL;
    buf->data = (char *)(buf + 1);
    buf->size = size;
    return buf;
}

void outbuf_destroy (struct outbuf *buf)
{
    if (buf) {
        int saved_errno = errno;
        free (buf);
        errno = saved_errno;
    }
}

// vi:ts=4 sw=4 expandtab
