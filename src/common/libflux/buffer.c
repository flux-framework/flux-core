/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "buffer.h"
#include "buffer_private.h"

#include "src/common/liblsd/cbuf.h"

#define FLUX_BUFFER_MAGIC 0xeb4feb4f

enum {
    FLUX_BUFFER_CB_TYPE_NONE,
    FLUX_BUFFER_CB_TYPE_READ,
    FLUX_BUFFER_CB_TYPE_READ_LINE,
    FLUX_BUFFER_CB_TYPE_WRITE,
};

struct flux_buffer {
    int magic;
    int size;
    bool readonly;
    cbuf_t cbuf;
    char *buf;                  /* internal buffer for user reads */
    int buflen;
    int cb_type;
    flux_buffer_cb cb;
    int cb_len;
    void *cb_arg;
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
    fb->size = size;
    fb->readonly = false;

    if (!(fb->cbuf = cbuf_create (fb->size, fb->size)))
        goto cleanup;

    if (cbuf_opt_set (fb->cbuf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP) < 0)
        goto cleanup;

    /* +1 for possible NUL on line reads */
    fb->buflen = size + 1;

    if (!(fb->buf = malloc (fb->buflen))) {
        errno = ENOMEM;
        goto cleanup;
    }

    fb->cb_type = FLUX_BUFFER_CB_TYPE_NONE;

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

int flux_buffer_size (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return fb->size;
}

int flux_buffer_bytes (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_used (fb->cbuf);
}

int flux_buffer_space (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return cbuf_free (fb->cbuf);
}

int flux_buffer_readonly (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    fb->readonly = true;
    return 0;
}

int flux_buffer_is_readonly (flux_buffer_t *fb)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return fb->readonly;
}

static int set_cb (flux_buffer_t *fb,
                   int cb_type,
                   flux_buffer_cb cb,
                   int cb_len,
                   void *cb_arg)
{
    if (fb->cb_type == FLUX_BUFFER_CB_TYPE_NONE) {
        if (!cb)
            return 0;

        if (cb_len < 0) {
            errno = EINVAL;
            return -1;
        }

        fb->cb_type = cb_type;
        fb->cb = cb;
        fb->cb_len = cb_len;
        fb->cb_arg = cb_arg;
    }
    else if (fb->cb_type == cb_type) {
        if (!cb) {
            fb->cb_type = FLUX_BUFFER_CB_TYPE_NONE;
            fb->cb = NULL;
            fb->cb_len = 0;
            fb->cb_arg = NULL;
        }
        else {
            if (cb_len < 0) {
                errno = EINVAL;
                return -1;
            }

            fb->cb_type = cb_type;
            fb->cb = cb;
            fb->cb_len = cb_len;
            fb->cb_arg = cb_arg;
        }
    }
    else {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

int flux_buffer_set_low_read_cb (flux_buffer_t *fb,
                                 flux_buffer_cb cb,
                                 int low,
                                 void *arg)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return set_cb (fb, FLUX_BUFFER_CB_TYPE_READ, cb, low, arg);
}

int flux_buffer_set_read_line_cb (flux_buffer_t *fb,
                                  flux_buffer_cb cb,
                                  void *arg)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return set_cb (fb, FLUX_BUFFER_CB_TYPE_READ_LINE, cb, 0, arg);
}

int flux_buffer_set_high_write_cb (flux_buffer_t *fb,
                                   flux_buffer_cb cb,
                                   int high,
                                   void *arg)
{
    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return set_cb (fb, FLUX_BUFFER_CB_TYPE_WRITE, cb, high, arg);
}

void check_write_cb (flux_buffer_t *fb)
{
    if (fb->cb_type == FLUX_BUFFER_CB_TYPE_WRITE
        && flux_buffer_bytes (fb) < fb->cb_len) {
        fb->cb (fb, fb->cb_arg);
    }
}

void check_read_cb (flux_buffer_t *fb)
{
    if (fb->cb_type == FLUX_BUFFER_CB_TYPE_READ
        && flux_buffer_bytes (fb) > fb->cb_len)
            fb->cb (fb, fb->cb_arg);
    else if (fb->cb_type == FLUX_BUFFER_CB_TYPE_READ_LINE
             && flux_buffer_lines (fb) > 0)
            fb->cb (fb, fb->cb_arg);
}

int flux_buffer_drop (flux_buffer_t *fb, int len)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_drop (fb->cbuf, len)) < 0)
        return -1;

    check_write_cb (fb);

    return ret;
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
    fb->buf[ret] = '\0';

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
    fb->buf[ret] = '\0';

    if (lenp)
        (*lenp) = ret;

    check_write_cb (fb);

    return fb->buf;
}

int flux_buffer_write (flux_buffer_t *fb, const void *data, int len)
{
    int ret;

    if (!fb
        || fb->magic != FLUX_BUFFER_MAGIC
        || !data
        || len < 0) {
        errno = EINVAL;
        return -1;
    }

    if (fb->readonly) {
        errno = EROFS;
        return -1;
    }

    if ((ret = cbuf_write (fb->cbuf, (void *)data, len, NULL)) < 0)
        return -1;

    check_read_cb (fb);

    return ret;
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
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_drop_line (fb->cbuf, fb->buflen, 1)) < 0)
        return -1;

    check_write_cb (fb);

    return ret;
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

const void *flux_buffer_peek_trimmed_line (flux_buffer_t *fb, int *lenp)
{
    int tmp_lenp = 0;

    if (!flux_buffer_peek_line (fb, &tmp_lenp))
        return NULL;

    if (tmp_lenp) {
        if (fb->buf[tmp_lenp - 1] == '\n') {
            fb->buf[tmp_lenp - 1] = '\0';
            tmp_lenp--;
        }
    }
    if (lenp)
        (*lenp) = tmp_lenp;

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

    check_write_cb (fb);

    return fb->buf;
}

const void *flux_buffer_read_trimmed_line (flux_buffer_t *fb, int *lenp)
{
    int tmp_lenp = 0;

    if (!flux_buffer_read_line (fb, &tmp_lenp))
        return NULL;

    if (tmp_lenp) {
        if (fb->buf[tmp_lenp - 1] == '\n') {
            fb->buf[tmp_lenp - 1] = '\0';
            tmp_lenp--;
        }
    }
    if (lenp)
        (*lenp) = tmp_lenp;

    return fb->buf;
}

int flux_buffer_write_line (flux_buffer_t *fb, const char *data)
{
    int ret;

    if (!fb
        || fb->magic != FLUX_BUFFER_MAGIC
        || !data) {
        errno = EINVAL;
        return -1;
    }

    if (fb->readonly) {
        errno = EROFS;
        return -1;
    }

    if ((ret = cbuf_write_line (fb->cbuf, (char *)data, NULL)) < 0)
        return -1;

    check_read_cb (fb);

    return ret;
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
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = cbuf_read_to_fd (fb->cbuf, fd, len)) < 0)
        return -1;

    check_write_cb (fb);

    return ret;
}

int flux_buffer_write_from_fd (flux_buffer_t *fb, int fd, int len)
{
    int ret;

    if (!fb || fb->magic != FLUX_BUFFER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if (fb->readonly) {
        errno = EROFS;
        return -1;
    }

    if ((ret = cbuf_write_from_fd (fb->cbuf, fd, len, NULL)) < 0)
        return -1;

    check_read_cb (fb);

    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
