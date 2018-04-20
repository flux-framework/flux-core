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
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "ebuf.h"

#include "src/common/liblsd/cbuf.h"

#define EBUF_MAGIC 0xeb4feb4f

enum {
    EBUF_CB_TYPE_NONE,
    EBUF_CB_TYPE_READ,
    EBUF_CB_TYPE_READ_LINE,
    EBUF_CB_TYPE_WRITE,
};

struct ebuf {
    int magic;
    cbuf_t cbuf;
    void *buf;                  /* internal buffer for user reads */
    int buflen;
    int cb_type;
    ebuf_cb cb;
    int cb_len;
    void *cb_arg;
};

ebuf_t *ebuf_create (int maxsize)
{
    ebuf_t *eb = NULL;

    if (maxsize <= 0) {
        errno = EINVAL;
        goto cleanup;
    }

    if (!(eb = calloc (1, sizeof (*eb)))) {
        errno = ENOMEM;
        goto cleanup;
    }

    eb->magic = EBUF_MAGIC;

    if (!(eb->cbuf = cbuf_create (maxsize, maxsize)))
        goto cleanup;

    if (cbuf_opt_set (eb->cbuf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) < 0)
        goto cleanup;

    /* +1 for possible NUL on line reads */
    eb->buflen = maxsize + 1;

    if (!(eb->buf = malloc (eb->buflen))) {
        errno = ENOMEM;
        goto cleanup;
    }

    eb->cb_type = EBUF_CB_TYPE_NONE;

    return eb;

cleanup:
    ebuf_destroy (eb);
    return NULL;
}

void ebuf_destroy (void *data)
{
    ebuf_t *eb = data;
    if (eb && eb->magic == EBUF_MAGIC) {
        eb->magic = ~EBUF_MAGIC;
        cbuf_destroy (eb->cbuf);
        free (eb->buf);
        free (eb);
    }
}

int ebuf_bytes (ebuf_t *eb)
{
    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    return cbuf_used (eb->cbuf);
}

int ebuf_set_cb (ebuf_t *eb, int cb_type, ebuf_cb cb, int cb_len, void *cb_arg)
{
    if (eb->cb_type == EBUF_CB_TYPE_NONE) {
        if (!cb)
            return 0;

        if (cb_len < 0) {
            errno = EINVAL;
            return -1;
        }

        eb->cb_type = cb_type;
        eb->cb = cb;
        eb->cb_len = cb_len;
        eb->cb_arg = cb_arg;
    }
    else if (eb->cb_type == cb_type) {
        if (!cb) {
            eb->cb_type = EBUF_CB_TYPE_NONE;
            eb->cb = NULL;
            eb->cb_len = 0;
            eb->cb_arg = NULL;
        }
        else {
            if (cb_len < 0) {
                errno = EINVAL;
                return -1;
            }

            eb->cb_type = cb_type;
            eb->cb = cb;
            eb->cb_len = cb_len;
            eb->cb_arg = cb_arg;
        }
    }
    else {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

int ebuf_set_low_read_cb (ebuf_t *eb, ebuf_cb cb, int low, void *arg)
{
    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return ebuf_set_cb (eb, EBUF_CB_TYPE_READ, cb, low, arg);
}

int ebuf_set_read_line_cb (ebuf_t *eb, ebuf_cb cb, void *arg)
{
    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return ebuf_set_cb (eb, EBUF_CB_TYPE_READ_LINE, cb, 0, arg);
}

int ebuf_set_high_write_cb (ebuf_t *eb, ebuf_cb cb, int high, void *arg)
{
    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return ebuf_set_cb (eb, EBUF_CB_TYPE_WRITE, cb, high, arg);
}

void check_write_cb (ebuf_t *eb)
{
    if (eb->cb_type == EBUF_CB_TYPE_WRITE
        && ebuf_bytes (eb) < eb->cb_len) {
        eb->cb (eb, eb->cb_arg);
    }
}

int ebuf_drop (ebuf_t *eb, int len)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_drop (eb->cbuf, len)) < 0)
        return -1;

    check_write_cb (eb);

    return ret;
}

const void *ebuf_peek (ebuf_t *eb, int len, int *lenp)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if (len < 0)
        len = cbuf_used (eb->cbuf);

    if (len > eb->buflen)
        len = eb->buflen;

    if ((ret = cbuf_peek (eb->cbuf, eb->buf, len)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return eb->buf;
}

const void *ebuf_read (ebuf_t *eb, int len, int *lenp)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if (len < 0)
        len = cbuf_used (eb->cbuf);

    if (len > eb->buflen)
        len = eb->buflen;

    if ((ret = cbuf_read (eb->cbuf, eb->buf, len)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    check_write_cb (eb);

    return eb->buf;
}

int ebuf_line (ebuf_t *eb)
{
    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_lines_used (eb->cbuf) > 0 ? 1 : 0;
}

int ebuf_drop_line (ebuf_t *eb)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_drop_line (eb->cbuf, eb->buflen, 1)) < 0)
        return -1;

    check_write_cb (eb);

    return ret;
}

const void *ebuf_peek_line (ebuf_t *eb, int *lenp)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if ((ret = cbuf_peek_line (eb->cbuf, eb->buf, eb->buflen, 1)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    return eb->buf;
}

const void *ebuf_read_line (ebuf_t *eb, int *lenp)
{
    int ret;

    if (!eb || eb->magic != EBUF_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if ((ret = cbuf_read_line (eb->cbuf, eb->buf, eb->buflen, 1)) < 0)
        return NULL;

    if (lenp)
        (*lenp) = ret;

    check_write_cb (eb);

    return eb->buf;
}

void check_read_cb (ebuf_t *eb)
{
    if (eb->cb_type == EBUF_CB_TYPE_READ
        && ebuf_bytes (eb) > eb->cb_len) {
        eb->cb (eb, eb->cb_arg);
    }
    else if (eb->cb_type == EBUF_CB_TYPE_READ_LINE
             && ebuf_line (eb) > 0) {
        eb->cb (eb, eb->cb_arg);
    }
}

int ebuf_write (ebuf_t *eb, const void *data, int len)
{
    int ret;

    if (!eb
        || eb->magic != EBUF_MAGIC
        || !data
        || len < 0) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_write (eb->cbuf, (void *)data, len, NULL)) < 0)
        return -1;

    check_read_cb (eb);

    return ret;
}

int ebuf_write_line (ebuf_t *eb, const char *data)
{
    int ret;

    if (!eb
        || eb->magic != EBUF_MAGIC
        || !data) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_write_line (eb->cbuf, (char *)data, NULL)) < 0)
        return -1;

    check_read_cb (eb);

    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
