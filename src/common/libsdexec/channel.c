/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* channel.c - manage stdio
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <flux/core.h>

#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fdutils.h"

#include "channel.h"

struct channel {
    flux_t *h;
    char rankstr[16];
    int fd[2];
    flux_watcher_t *w;
    bool eof_delivered;
    char *name;
    bool writable;
    channel_output_f output_cb;
    channel_error_f error_cb;
    void *arg;
};

/* fd watcher for read end of channel file descriptor
 */
static void channel_output_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    struct channel *ch = arg;
    char buf[1024];
    ssize_t n;
    json_t *io;

    if ((n = read (ch->fd[0], buf, sizeof (buf))) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return; // spurious wakeup or revents without POLLIN?
        if (ch->error_cb) {
            flux_error_t error;
            errprintf (&error,
                       "error reading from %s: %s",
                       ch->name,
                       strerror (errno));
            ch->error_cb (ch, &error, ch->arg);
            // fall through and generate EOF
        }
    }
    /* Since sdexec.exec clients are not finalized until the channel callback
     * gets EOF, ensure that it always does, even if there was a read error.
     */
    if (n <= 0) {
        io = ioencode (ch->name, ch->rankstr, NULL, 0, true);
        flux_watcher_stop (w);
        ch->eof_delivered = true;
    }
    else
        io = ioencode (ch->name, ch->rankstr, buf, n, false);
    if (!io) {
        if (ch->error_cb) {
            flux_error_t error;
            errprintf (&error,
                       "error encoding data from %s: %s",
                       ch->name,
                       strerror (errno));
            ch->error_cb (ch, &error, ch->arg);
        }
        return;
    }
    if (ch->output_cb)
        ch->output_cb (ch, io, ch->arg);
    json_decref (io);
}

int sdexec_channel_get_fd (struct channel *ch)
{
    return ch ? ch->fd[1] : -1;
}

const char *sdexec_channel_get_name (struct channel *ch)
{
    return ch ? ch->name : "unknown";
}

void sdexec_channel_close_fd (struct channel *ch)
{
    if (ch && ch->fd[1] >= 0) {
        close (ch->fd[1]);
        ch->fd[1] = -1;
    }
}

void sdexec_channel_start_output (struct channel *ch)
{
    if (ch && !ch->eof_delivered)
        flux_watcher_start (ch->w);
}

void sdexec_channel_destroy (struct channel *ch)
{
    if (ch) {
        int saved_errno = errno;
        if (ch->fd[0] >= 0)
            close (ch->fd[0]);
        if (ch->fd[1] >= 0)
            close (ch->fd[1]);
        flux_watcher_destroy (ch->w);
        free (ch->name);
        free (ch);
        errno = saved_errno;
    }
}

static struct channel *sdexec_channel_create (flux_t *h, const char *name)
{
    struct channel *ch;
    uint32_t rank;

    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ch = calloc (1, sizeof (*ch))))
        return NULL;
    ch->h = h;
    ch->fd[0] = -1;
    ch->fd[1] = -1;
    if (!(ch->name = strdup (name)))
        goto error;
    if (flux_get_rank (h, &rank) < 0)
        goto error;
    snprintf (ch->rankstr, sizeof (ch->rankstr), "%u", (unsigned int)rank);
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, ch->fd) < 0)
        goto error;
    return ch;
error:
    sdexec_channel_destroy (ch);
    return NULL;
}

struct channel *sdexec_channel_create_output (flux_t *h,
                                              const char *name,
                                              channel_output_f output_cb,
                                              channel_error_f error_cb,
                                              void *arg)
{
    struct channel *ch;

    if (!(ch = sdexec_channel_create (h, name)))
        return NULL;
    ch->output_cb = output_cb;
    ch->error_cb = error_cb;
    ch->arg = arg;
    if (fd_set_nonblocking (ch->fd[0]) < 0)
        goto error;
    if (!(ch->w = flux_fd_watcher_create (flux_get_reactor (h),
                                          ch->fd[0],
                                          FLUX_POLLIN,
                                          channel_output_cb,
                                          ch)))
        goto error;
    return ch;
error:
    sdexec_channel_destroy (ch);
    return NULL;
}

struct channel *sdexec_channel_create_input (flux_t *h, const char *name)
{
    struct channel *ch;

    if (!(ch = sdexec_channel_create (h, name)))
        return NULL;
    ch->writable = true;
    return ch;
}

int sdexec_channel_write (struct channel *ch, json_t *io)
{
    char *data;
    int len;
    bool eof;

    if (!ch || !io) {
        errno = EINVAL;
        return -1;
    }
    if (iodecode (io, NULL, NULL, &data, &len, &eof) < 0)
        return -1;
    if (!ch->writable || ch->fd[0] < 0) {
        errno = EINVAL;
        return -1;
    }
    if (data && len > 0) {
        int count = 0;
        while (count < len) {
            ssize_t n;
            if ((n = write (ch->fd[0], data + count, len - count)) < 0) {
                ERRNO_SAFE_WRAP (free, data);
                return -1;
            }
            count += n;
        }
        free (data);
    }
    if (eof) {
        int fd = ch->fd[0];

        ch->fd[0] = -1;
        if (close (fd) < 0)
            return -1;
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
