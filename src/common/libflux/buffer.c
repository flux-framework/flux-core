/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "buffer.h"

#include "src/common/liblsd/cbuf.h"

#define FLUX_BUFFER_MAGIC 0xeb4feb4f

struct flux_buffer {
    int magic;
    cbuf_t cbuf;
    void *buf;                  /* internal buffer for user reads */
    int buflen;
};

flux_buffer_t *flux_buffer_create (int size)
{
    flux_buffer_t *fb = NULL;

    if (size <= 0) {
        errno = EINVAL;
        goto cleanup;
    }

    if (!(fb = calloc (1, sizeof (*fb)))) {
        errno = ENOMEM;
        goto cleanup;
    }

    fb->magic = FLUX_BUFFER_MAGIC;

    if (!(fb->cbuf = cbuf_create (size, size)))
        goto cleanup;

    if (cbuf_opt_set (fb->cbuf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) < 0)
        goto cleanup;

    /* +1 for possible NUL on line reads */
    fb->buflen = size + 1;

    if (!(fb->buf = malloc (fb->buflen))) {
        errno = ENOMEM;
        goto cleanup;
    }

    return fb;

cleanup:
    flux_buffer_destroy (fb);
    return NULL;
}

void flux_buffer_destroy (void *data)
{
    flux_buffer_t *fb = data;
    if (fb && fb->magic == FLUX_BUFFER_MAGIC) {
        fb->magic = ~FLUX_BUFFER_MAGIC;
        cbuf_destroy (fb->cbuf);
        free (fb->buf);
        free (fb);
    }
}

int flux_buffer_bytes (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_used (fb->cbuf);
}

int flux_buffer_drop (flux_buffer_t *fb, int len)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_drop (fb->cbuf, len);
}

const void *flux_buffer_peek (flux_buffer_t *fb, int len, int *lenp)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if (len < 0)
        len = cbuf_used (fb->cbuf);

    if (len > fb->buflen)
        len = fb->buflen;

    if ((ret = cbuf_peek (fb->cbuf, fb->buf, len)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return fb->buf;
}

const void *flux_buffer_read (flux_buffer_t *fb, int len, int *lenp)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if (len < 0)
        len = cbuf_used (fb->cbuf);

    if (len > fb->buflen)
        len = fb->buflen;

    if ((ret = cbuf_read (fb->cbuf, fb->buf, len)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return fb->buf;
}

int flux_buffer_write (flux_buffer_t *fb, const void *data, int len)
{
    if (!fb
        || fb->magic != FLUX_BUFFER_MAGIC
        || !data
        || len < 0) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_write (fb->cbuf, (void *)data, len, NULL);
}

int flux_buffer_lines (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_lines_used (fb->cbuf);
}

int flux_buffer_drop_line (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_drop_line (fb->cbuf, fb->buflen, 1);
}

const void *flux_buffer_peek_line (flux_buffer_t *fb, int *lenp)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if ((ret = cbuf_peek_line (fb->cbuf, fb->buf, fb->buflen, 1)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return fb->buf;
}

const void *flux_buffer_read_line (flux_buffer_t *fb, int *lenp)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if ((ret = cbuf_read_line (fb->cbuf, fb->buf, fb->buflen, 1)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return fb->buf;
}

int flux_buffer_write_line (flux_buffer_t *fb, const char *data)
{
    if (!fb
        || fb->magic != FLUX_BUFFER_MAGIC
        || !data) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_write_line (fb->cbuf, (char *)data, NULL);
}

int flux_buffer_peek_to_fd (flux_buffer_t *fb, int fd, int len)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_peek_to_fd (fb->cbuf, fd, len);
}

int flux_buffer_read_to_fd (flux_buffer_t *fb, int fd, int len)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_read_to_fd (fb->cbuf, fd, len);
}

int flux_buffer_write_from_fd (flux_buffer_t *fb, int fd, int len)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_write_from_fd (fb->cbuf, fd, len, NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
