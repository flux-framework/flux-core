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
#include "src/common/libsubprocess/subprocess_private.h" // for default bufsize

#include "outbuf.h"
#include "channel.h"

struct channel {
    flux_t *h;
    char rankstr[16];
    int fd[2];
    flux_watcher_t *w;
    bool eof_received;
    bool eof_delivered;
    struct outbuf *buf;
    int flags;
    char *name;
    bool writable;
    channel_output_f output_cb;
    channel_error_f error_cb;
    void *arg;
    int refcount;
};

static struct channel *sdexec_channel_incref (struct channel *ch);
static void sdexec_channel_decref (struct channel *ch);

static int call_output_callback (struct channel *ch,
                                 const char *data,
                                 size_t length,
                                 bool eof)
{
    json_t *io;
    int rc = -1;

    if (length == 0)
        data = NULL; // appease ioencode()
    if (!(io = ioencode (ch->name, ch->rankstr, data, length, eof)))
        goto done;
    if (ch->output_cb)
        ch->output_cb (ch, io, ch->arg);
    if (eof)
        ch->eof_delivered = true;
    rc = 0;
done:
    json_decref (io);
    return rc;
}

static size_t nextline (const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n')
            return i + 1;
    }
    return 0;
}

/* Flush one line, or one partial buffer if it meets criteria noted below.
 * This function returns -1 on error, 0 if done, or 1 if it should be called
 * again.
 */
static int flush_output_line (struct channel *ch)
{
    size_t len;
    bool eof = false;

    len = nextline (outbuf_tail (ch->buf), outbuf_used (ch->buf));
    /* There is no complete line, but the buffer is full.
     * No more data can be added to terminate the line so we must flush.
     */
    if (len == 0 && outbuf_full (ch->buf))
        len = outbuf_used (ch->buf);
    /* There is no complete line nor full buffer, but EOF has been reached.
     * No more data will ever be added to terminate the line so we must flush.
     */
    if (len == 0 && ch->eof_received) {
        len = outbuf_used (ch->buf);
        eof = true;
    }
    if (len > 0 || eof) {
        int rc = call_output_callback (ch, outbuf_tail (ch->buf), len, eof);
        outbuf_mark_free (ch->buf, len);
        if (rc < 0)
            return -1;
    }
    if (len == 0 || eof)
        return 0;
    return 1;
}

/* Flush all data in the buffer.
 */
static int flush_output_raw (struct channel *ch)
{
    int n;
    n = call_output_callback (ch,
                              outbuf_tail (ch->buf),
                              outbuf_used (ch->buf),
                              ch->eof_received);
    outbuf_mark_free (ch->buf, outbuf_used (ch->buf));
    return n;
}

/* fd watcher for read end of channel file descriptor
 */
static void channel_output_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    struct channel *ch = arg;
    ssize_t n;

    /* Read a chunk of data into the buffer, not necessarily all that is ready.
     * Let the event loop iterate and read more as needed.
     */
    n = read (ch->fd[0], outbuf_head (ch->buf), outbuf_free (ch->buf));
    if (n < 0) {
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
        ch->eof_received = true;
        flux_watcher_stop (w);
    }
    else
        outbuf_mark_used (ch->buf, n);
    /* In case the channel output callback destroys the channel,
     * hold a reference for the remainder of this function.
     * See flux-framework/flux-core#6036.
     */
    sdexec_channel_incref (ch);
    if ((ch->flags & CHANNEL_LINEBUF)) {
        while ((n = flush_output_line (ch)) > 0)
            ;
    }
    else
        n = flush_output_raw (ch);
    if (n < 0) {
        if (ch->error_cb) {
            flux_error_t error;
            errprintf (&error,
                       "error flushing data from %s: %s",
                       ch->name,
                       strerror (errno));
            ch->error_cb (ch, &error, ch->arg);
        }
    }
    outbuf_gc (ch->buf);
    sdexec_channel_decref (ch);
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

static struct channel *sdexec_channel_incref (struct channel *ch)
{
    if (ch)
        ch->refcount++;
    return ch;
}

static void sdexec_channel_decref (struct channel *ch)
{
    if (ch && --ch->refcount == 0) {
        int saved_errno = errno;
        if (ch->fd[0] >= 0)
            close (ch->fd[0]);
        if (ch->fd[1] >= 0)
            close (ch->fd[1]);
        flux_watcher_destroy (ch->w);
        outbuf_destroy (ch->buf);
        free (ch->name);
        free (ch);
        errno = saved_errno;
    }
}

void sdexec_channel_destroy (struct channel *ch)
{
    sdexec_channel_decref (ch);
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
    ch->refcount = 1;
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
                                              size_t bufsize,
                                              int flags,
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
    ch->flags = flags;
    if (fd_set_nonblocking (ch->fd[0]) < 0)
        goto error;
    if (!(ch->w = flux_fd_watcher_create (flux_get_reactor (h),
                                          ch->fd[0],
                                          FLUX_POLLIN,
                                          channel_output_cb,
                                          ch)))
        goto error;
    if (bufsize == 0)
        bufsize = SUBPROCESS_DEFAULT_BUFSIZE;
    if (!(ch->buf = outbuf_create (bufsize)))
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

json_t *sdexec_channel_get_stats (struct channel *ch)
{
    json_t *o = NULL;

    if (ch) {
        if (ch->writable) {
            o = json_pack ("{s:i s:i}",
                           "local_fd", ch->fd[0],
                           "remote_fd", ch->fd[1]);
        }
        else {
            o = json_pack ("{s:i s:i s:i s:i s:b}",
                           "local_fd", ch->fd[0],
                           "remote_fd", ch->fd[1],
                           "buf_used", outbuf_used (ch->buf),
                           "buf_free", outbuf_free (ch->buf),
                           "eof", ch->eof_received);
        }
    }
    return o;
}

// vi:ts=4 sw=4 expandtab
