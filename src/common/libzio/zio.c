/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include <sodium.h>

#include "src/common/liblsd/cbuf.h"
#include "src/common/libutil/macros.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/fdutils.h"

#include "zio.h"

#ifndef NDEBUG
#  define ZIO_MAGIC         0x510015
#endif

#define ZIO_EOF             (1<<0)
#define ZIO_EOF_SENT        (1<<1)
#define ZIO_BUFFERED        (1<<2)
#define ZIO_LINE_BUFFERED   (1<<4)
#define ZIO_CLOSED          (1<<5)
#define ZIO_VERBOSE         (1<<6)
#define ZIO_IN_HANDLER      (1<<7)
#define ZIO_DESTROYED       (1<<8)
#define ZIO_RAW_OUTPUT      (1<<9)

#define ZIO_READER          1
#define ZIO_WRITER          2

struct zio_ctx {
#ifndef NDEBUG
    int       magic;
#endif /* !NDEBUG */
    char *     name;    /*  Name of this io context (used in json encoding)  */

    char *     prefix;  /*  Prefix for debug output                          */
    zio_log_f  log_f;   /*  Debug output function                            */

    int        io_type; /*  "Reader" reads from fd and sends json
                            "Writer" reads json and writes to fd             */
    int        srcfd;   /*  srcfd for ZIO "reader"                           */
    int        dstfd;   /*  dstfd for ZIO "writer"                           */
    cbuf_t     buf;     /*  Buffer for I/O (if needed)                       */
    size_t     buffersize;
    int        lines;   /*  For line buffered, arg to cbuf_read_line()       */

    unsigned   flags;   /*  Flags for state and options of zio object        */

    zio_send_f send;    /*  Callback to send json data                       */
    zio_close_f close;  /*  Callback after eof is sent                       */

    flux_reactor_t *reactor;
    flux_watcher_t *reader;
    flux_watcher_t *writer;
    void *arg;          /*  Arg passed to callbacks                          */
};

static void zio_vlog (zio_t *zio, const char *format, va_list ap)
{
    char  buf[4096];
    char *p;
    char *prefix;
    int   n;
    int   len;

    p = buf;
    len = sizeof (buf);

    n = snprintf (p, len, "ZIO: ");
    p += n;
    len -= n;

    prefix = zio->prefix ? zio->prefix : zio->name;
    if ((len > 0) && prefix) {
        n = snprintf (p, len, "%s: ", prefix);
        if ((n < 0) || (n >= len)) {
            p += len - 1;
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

    if ((len > 0) && (format)) {
        n = vsnprintf (p, len, format, ap);
        if ((n < 0) || (n >= len)) {
            p += len - 1;
            len = 0;
        }
        else {
            p += n;
            len -= n;
        }
    }

     /*  Add suffix for truncation if necessary.
      */
    if (len <= 0) {
        char *q;
        const char *suffix = "+";
        q = buf + sizeof (buf) - 1 - strlen (suffix);
        p = (p < q) ? p : q;
        strcpy (p, suffix);
        p += strlen (suffix);
    }

    *p = '\0';
    if (zio->log_f)
        (*zio->log_f) (buf);
    else
        fputs (buf, stderr);

    return;
}

static int zio_verbose (zio_t *zio)
{
    return (zio->flags & ZIO_VERBOSE);
}

static void zio_debug (zio_t *zio, const char *fmt, ...)
{
    if (zio_verbose (zio)) {
        va_list ap;
        va_start (ap, fmt);
        zio_vlog (zio, fmt, ap);
        va_end (ap);
    }
}

static inline void zio_set_destroyed (zio_t *zio)
{
    zio->flags |= ZIO_DESTROYED;
}

static inline int zio_is_destroyed (zio_t *zio)
{
    return (zio->flags & ZIO_DESTROYED);
}

static inline int zio_is_in_handler (zio_t *zio)
{
    return (zio->flags & ZIO_IN_HANDLER);
}

static inline void zio_handler_start (zio_t *zio)
{
    zio->flags |= ZIO_IN_HANDLER;
}

static inline void zio_handler_end (zio_t *zio)
{
    zio->flags &= ~ZIO_IN_HANDLER;
    if (zio_is_destroyed (zio))
        zio_destroy (zio);
}

void zio_destroy (zio_t *z)
{
    if (z == NULL)
        return;
    assert (z->magic == ZIO_MAGIC);
    if (zio_is_in_handler (z)) {
        zio_set_destroyed (z);
        return;
    }
    if (z->buf)
        cbuf_destroy (z->buf);
    free (z->name);
    free (z->prefix);
    zio_close_src_fd (z);
    zio_close_dst_fd (z);
    flux_watcher_destroy (z->reader);
    flux_watcher_destroy (z->writer);
    assert ((z->magic = ~ZIO_MAGIC));
    free (z);
}

static int zio_init_buffer (zio_t *zio)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    assert (zio->buf == NULL);

    if (zio->buf)
        cbuf_destroy (zio->buf);
    if (!(zio->buf = cbuf_create (zio->buffersize, 1638400)))
        return (-1);

    cbuf_opt_set (zio->buf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
    return (0);
}


static zio_t *zio_allocate (const char *name, int reader, void *arg)
{
    zio_t *z;

    if (!name) {
        errno = EINVAL;
        return (NULL);
    }

    if (!(z = malloc (sizeof (*z))))
        return NULL;

    memset (z, 0, sizeof (*z));
    assert ((z->magic = ZIO_MAGIC));

    if (!(z->name = strdup (name))) {
        zio_destroy (z);
        return (NULL);
    }

    z->arg = arg;
    z->io_type = reader ? ZIO_READER : ZIO_WRITER;
    z->flags = ZIO_BUFFERED | ZIO_LINE_BUFFERED;
    z->buffersize = 4096;
    z->lines = -1;

    z->srcfd = z->dstfd = -1;
    z->prefix = NULL;

    zio_init_buffer (z);

    return (z);
}

/*
 *  zio reader reads from srcfd and generates json data to sendfn
 */
int zio_reader (zio_t *zio)
{
    return (zio->io_type == ZIO_READER);
}

/*
 *  zio writer consumes data in json and sends to dstfd
 */
int zio_writer (zio_t *zio)
{
    return (zio->io_type == ZIO_WRITER);
}

static inline void zio_clear_buffered (zio_t *zio)
{
    zio->flags &= ~(ZIO_LINE_BUFFERED | ZIO_BUFFERED);
}

static inline int zio_line_buffered (zio_t *zio)
{
    return (zio->flags & ZIO_LINE_BUFFERED);
}

static inline int zio_buffered (zio_t *zio)
{
    return (zio->flags & ZIO_BUFFERED);
}


static inline void zio_set_eof (zio_t *zio)
{
    zio->flags |= ZIO_EOF;
}

static inline int zio_eof (zio_t *zio)
{
    return (zio->flags & ZIO_EOF);
}

static int zio_eof_pending (zio_t *zio)
{
    /* Already closed? Then EOF can't be pending */
    if (zio_closed (zio))
      return (0);
    /*
     *   zio object has EOF pending if EOF flag is set and either the
     *    io is unbuffered or the buffer for IO is empty.
     */
    return (zio_eof (zio) && (!cbuf_used (zio->buf)));
}

static int zio_buffer_used (zio_t *zio)
{
    return cbuf_used (zio->buf);
}

static int zio_buffer_empty (zio_t *zio)
{
    return (!zio_buffered (zio) || (cbuf_used (zio->buf) == 0));
}

static int zio_eof_sent (zio_t *zio)
{
    return (zio->flags & ZIO_EOF_SENT);
}

int zio_set_unbuffered (zio_t *zio)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);

    zio_clear_buffered (zio);
    if (zio->buf) {
        /* XXX: Drain buffer or set it to drain, then destroy */
    }
    return (0);
}

int zio_set_buffered (zio_t *zio, size_t buffersize)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    zio->flags |= ZIO_BUFFERED;
    if (!zio->buf)
        return zio_init_buffer (zio);
    return (0);
}

int zio_set_line_buffered (zio_t *zio, int lines)
{
    int rc = zio_set_buffered (zio, 4096);
    zio->flags |= ZIO_LINE_BUFFERED;
    zio->lines = lines;
    return (rc);
}

static int zio_set_verbose (zio_t *zio)
{
    zio->flags |= ZIO_VERBOSE;
    return (0);
}

int zio_set_quiet (zio_t *zio)
{
    zio->flags &= (~ZIO_VERBOSE);
    return (0);
}

int zio_set_debug (zio_t *zio, const char *prefix, zio_log_f logf)
{
    if (!zio || zio->magic != ZIO_MAGIC)
        return (-1);
    zio_set_verbose (zio);

    if (prefix)
        zio->prefix = strdup (prefix);
    if (logf)
        zio->log_f = logf;
    return (0);
}

int zio_set_raw_output (zio_t *zio)
{
    if (!zio || zio->magic != ZIO_MAGIC)
        return (-1);
    zio->flags |= ZIO_RAW_OUTPUT;
    return (0);
}

int zio_set_send_cb (zio_t *zio, zio_send_f sendf)
{
    zio->send = sendf;
    return (0);
}

int zio_set_close_cb (zio_t *zio, zio_close_f closef)
{
    zio->close = closef;
    return (0);
}

static int zio_fd_read (zio_t *zio, void *dst, int len)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    assert (zio->buf);

    if (zio_line_buffered (zio) && !zio_eof (zio))
        return cbuf_read_line (zio->buf, dst, len, zio->lines);
    else
        return cbuf_read (zio->buf, dst, len);
}

static char *zio_json_str_create (zio_t *zio, void *data, size_t len)
{
    bool eof = false;
    if (zio_eof_pending (zio)) {
        eof = true;
        zio_debug (zio, "Setting EOF sent\n");
        zio->flags |= ZIO_EOF_SENT;
    }
    return zio_json_encode (data, len, eof);
}

static int zio_send (zio_t *zio, char *p, size_t len)
{
    int rc = -1;
    char *json_str = NULL;

    zio_debug (zio, "zio_send (len=%d)\n", len);

    if (!zio->send)
        return rc;

    if (!(zio->flags & ZIO_RAW_OUTPUT)) {
        if (!(json_str = zio_json_str_create (zio, p, len)))
            goto done;
        p = json_str;
    }
    rc = (*zio->send) (zio, p, len, zio->arg);
    if (rc >= 0 && len == 0)
        zio->flags |= ZIO_EOF_SENT;
done:
    if (json_str)
        free (json_str);
    return rc;
}

static int zio_data_to_flush (zio_t *zio)
{
    int size;

    if ((size = zio_buffer_used (zio)) <= 0)
        return (0);

    /*
     *   For unbuffered IO we will flush all data. For line buffered
     *    IO we will read all available lines. In both cases, return
     *    the amount of data currently waiting in the buffer.
     */
    if (!zio_buffered (zio) || zio_line_buffered (zio))
        return (size);

    /*  For normal buffered IO, we will only flush data when availble
     *   bytes are greater than the current buffer size, unless there
     *   is a pending EOF
     */
    if (zio_eof (zio) || (size <= zio->buffersize))
        return (size);

    return (0);
}

int zio_closed (zio_t *zio)
{
    if (zio->flags & ZIO_EOF_SENT)
        return (1);
    return (0);
}

int zio_close_src_fd (zio_t *zio)
{
    if (zio->srcfd >= 0) {
        if (close (zio->srcfd) < 0) {
            zio_debug (zio, "close srcfd: %s", strerror (errno));
            return -1;
        }
        zio->srcfd = -1;
    }
    return 0;
}

int zio_close_dst_fd (zio_t *zio)
{
    if (zio->dstfd >= 0) {
        if (close (zio->dstfd) < 0) {
            zio_debug (zio, "close srcfd: %s", strerror (errno));
            return -1;
        }
        zio->dstfd = -1;
    }
    return 0;
}

static int zio_close (zio_t *zio)
{
    if (zio->flags & ZIO_CLOSED) {
        /* Already closed */
        errno = EINVAL;
        return (-1);
    }
    zio_debug (zio, "zio_close\n");
    if (zio_reader (zio))
        zio_close_src_fd (zio);
    else if (zio_writer (zio)) {
        zio_close_dst_fd (zio);
        /* For writer zio object, consider close(dstfd)
         *  as "EOF sent"
         */
        zio->flags |= ZIO_EOF_SENT;
    }
    zio->flags |= ZIO_CLOSED;
    if (zio->close)
        return (*zio->close) (zio, zio->arg);
    return (0);
}

static int zio_writer_flush_all (zio_t *zio)
{
    int n = 0;
    zio_debug (zio, "zio_writer_flush_all: used=%d\n", zio_buffer_used (zio));
    while (zio_buffer_used (zio) > 0) {
        int rc = cbuf_read_to_fd (zio->buf, zio->dstfd, -1);
        zio_debug (zio, "zio_writer_flush_all: rc=%d\n", rc);
        if (rc < 0)
            return (rc);
        n += rc;
    }
    zio_debug (zio, "zio_writer_flush_all: n=%d\n", n);
    if (zio_buffer_used (zio) == 0 && zio_eof_pending (zio))
        zio_close (zio);
    return (n);
}

int zio_flush (zio_t *zio)
{
    int len;
    int rc = 0;

    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (-1);
    if (zio_reader (zio) && !zio->send)
       return (-1);

    zio_debug (zio, "zio_flush\n");

    /*
     *  Nothing to flush if EOF already sent to consumer:
     */
    if (zio_eof_sent (zio))
        return (0);

    if (zio_writer (zio))
        return zio_writer_flush_all (zio);

    /* else zio reader:
    */

    while (((len = zio_data_to_flush (zio)) > 0) || zio_eof (zio)) {
        char * buf = NULL;
        int n = 0;
        zio_debug (zio, "zio_flush: len = %d, eof = %d\n", len, zio_eof (zio));
        if (len > 0) {
            buf = xzmalloc (len + 1);
            if ((n = zio_fd_read (zio, buf, len + 1)) <= 0) {
                if (n < 0) {
                    zio_debug (zio, "zio_read: %s", strerror (errno));
                    rc = -1;
                }
                /*
                 *  We may not be able to read any data from the buffer
                 *   because we are line buffering and there is not yet
                 *   a full line in the buffer. In this case just exit
                 *   so we can buffer more data.
                 */
                free (buf);
                return (rc);

            }
        }
        zio_debug (zio, "zio_data_to_flush = %d\n", zio_data_to_flush (zio));
        zio_debug (zio, "zio_flush: Sending %d (%s) [eof=%d]\n", n, buf, zio_eof(zio));
        rc = zio_send (zio, buf, n);
        if (buf)
            free (buf);
        if (zio_eof_sent (zio))
            break;
    }
    return (rc);
}


int zio_read (zio_t *zio)
{
    int n;
    assert ((zio != NULL) && (zio->magic == ZIO_MAGIC));
    if ((n = cbuf_write_from_fd (zio->buf, zio->srcfd, -1, NULL)) < 0)
        return (-1);

    zio_debug (zio, "zio_read: read = %d\n", n);

    if (n == 0) {
        zio_set_eof (zio);
        zio_debug (zio, "zio_read_cb: Got eof\n");
    }

    zio_flush (zio);

    return (n);
}

static int zio_read_cb_common (zio_t *zio)
{
    int rc = zio_read (zio);
    if ((rc < 0) && (errno == EAGAIN))
        rc = 0;
    return (rc);
}

static void zio_flux_read_cb (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    zio_t *zio = arg;
    int rc;
    zio_handler_start (zio);
    rc = zio_read_cb_common (zio);
    if (rc >= 0 && zio_eof_sent (zio)) {
        zio_debug (zio, "reader detaching from flux reactor\n");
        flux_watcher_stop (w);
        rc = zio_close (zio);
    }
    zio_handler_end (zio);
    if (rc < 0)
        flux_reactor_stop_error (r);
}


static int zio_write_pending (zio_t *zio)
{
    if (zio_closed (zio))
        return (0);

    if ((zio_buffer_used (zio) > 0) || zio_eof (zio))
        return (1);

    return (0);
}

/*
 *  Callback when zio->dstfd is writeable. Write buffered data to
 *   file descriptor.
 */
static int zio_writer_cb (zio_t *zio)
{
    int rc = 0;

    if (cbuf_used (zio->buf))
        rc = cbuf_read_to_fd (zio->buf, zio->dstfd, -1);
    if (rc < 0) {
        if (errno == EAGAIN)
            return (0);
        zio_debug (zio, "cbuf_read_to_fd: %s\n", strerror (errno));
        return (-1);
    }
    if ((rc == 0) && zio_eof_pending (zio))
        rc = zio_close (zio);
    return (rc);
}

static void zio_flux_writer_cb (flux_reactor_t *r, flux_watcher_t *w,
                                int revents, void *arg)
{
    zio_t *zio = arg;
    int rc;
    zio_handler_start (zio);
    rc = zio_writer_cb (zio);
    if (!zio_write_pending (zio))
        flux_watcher_stop (w);
    zio_handler_end (zio);
    if (rc < 0)
        flux_reactor_stop_error (r);
}

static int zio_flux_reader_poll (zio_t *zio)
{
    if (!zio->reactor)
        return (-1);
    if (!zio->reader)
        zio->reader = flux_fd_watcher_create (zio->reactor,
                zio->srcfd, FLUX_POLLIN, zio_flux_read_cb, zio);
    if (!zio->reader)
        return (-1);
    flux_watcher_start (zio->reader);
    return (0);
}

static int zio_reader_poll (zio_t *zio)
{
    if (zio->reactor)
        return zio_flux_reader_poll (zio);
    return (-1);
}

/*
 *  Schedule pending data to write to zio->dstfd
 */
static int zio_flux_writer_schedule (zio_t *zio)
{
    if (!zio->reactor)
        return (-1);
    if (!zio->writer)
        zio->writer = flux_fd_watcher_create (zio->reactor,
                zio->dstfd, FLUX_POLLOUT, zio_flux_writer_cb, zio);
    if (!zio->writer)
        return (-1);
    flux_watcher_start (zio->writer);
    return (0);
}

static int zio_writer_schedule (zio_t *zio)
{
    if (zio->reactor)
        return zio_flux_writer_schedule (zio);
    return (-1);
}

/*
 *  write data into zio buffer
 */
static int zio_write_data (zio_t *zio, void *buf, size_t len)
{
    int n = 0;
    int ndropped = 0;

    /*
     *  If buffer is empty, first try writing directly to dstfd
     *   to avoid double-copy:
     */
    if (zio_buffer_empty (zio)) {
        n = write (zio->dstfd, buf, len);
        if (n < 0) {
            if (errno == EAGAIN)
                n = 0;
            else
                return (-1);
        }
        /*
         *  If we wrote everything, return early
         */
        if (n == len) {
            if (zio_eof (zio))
                zio_close (zio);
            return (len);
        }
    }

    /*
     *  Otherwise, buffer any remaining data
     */
    if ((len - n) && cbuf_write (zio->buf, buf+n, len-n, &ndropped) < 0)
        return (-1);

    return (0);
}

static int zio_write_internal (zio_t *zio, void *data, size_t len)
{
    int rc;

    rc = zio_write_data (zio, data, len);
    zio_debug (zio, "zio_write: %d bytes, eof=%d\n", len, zio_eof (zio));

    if (zio_write_pending (zio))
        zio_writer_schedule (zio);
    return (rc);
}

int zio_write (zio_t *zio, void *data, size_t len)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC) || !zio_writer (zio)) {
        errno = EINVAL;
        return (-1);
    }

    if (!data || len <= 0) {
        errno = EINVAL;
        return (-1);
    }

    return (zio_write_internal (zio, data, len));
}

int zio_write_eof (zio_t *zio)
{
     if ((zio == NULL) || (zio->magic != ZIO_MAGIC) || !zio_writer (zio)) {
        errno = EINVAL;
        return (-1);
    }
    zio_set_eof (zio);
    /* If no data is buffered, then we can close the dst fd:
     */
    if (zio_buffer_empty (zio))
        zio_close (zio);
    return (0);
}

/*
 *  Write json string to this zio object, buffering unwritten data.
 */
int zio_write_json (zio_t *zio, const char *json_str)
{
    char *s = NULL;
    int len, rc = 0;
    bool eof;

    if ((zio == NULL) || (zio->magic != ZIO_MAGIC) || !zio_writer (zio)) {
        errno = EINVAL;
        return (-1);
    }
    len = zio_json_decode (json_str, (void **)&s, &eof);
    if (len < 0) {
        errno = EINVAL;
        return (-1);
    }
    if (eof)
        zio_set_eof (zio);
    if (len > 0)
        rc = zio_write_internal (zio, s, len);
    else if (zio_write_pending (zio))
        zio_writer_schedule (zio);

    free (s);
    return rc;
}

static int zio_bootstrap (zio_t *zio)
{
    if (zio_reader (zio))
        zio_reader_poll (zio);
    else if (zio_writer (zio)) {
        /*
         *  Add writer to poll loop only if there is data pending to
         *   be written
         */
        if (zio_write_pending (zio))
            zio_writer_schedule (zio);
    }
    return (0);
}

int zio_reactor_attach (zio_t *zio, flux_reactor_t *r)
{
    errno = EINVAL;
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (-1);

    zio->reactor = r;
    return (zio_bootstrap (zio));
}

int zio_flux_attach (zio_t *zio, flux_t *h)
{
    return zio_reactor_attach (zio, flux_get_reactor (h));
}

zio_t *zio_reader_create (const char *name, int srcfd, void *arg)
{
    zio_t *zio = zio_allocate (name, 1, arg);

    zio->srcfd = srcfd;
    fd_set_nonblocking (zio->srcfd);
    zio->send = NULL;
    return (zio);
}

zio_t *zio_pipe_reader_create (const char *name, void *arg)
{
    zio_t *zio;
    int pfds[2];

    if (pipe (pfds) < 0)
        return (NULL);

    if ((zio = zio_reader_create (name, pfds[0], arg)) == NULL) {
        close (pfds[0]);
        close (pfds[1]);
        return (NULL);
    }
    zio->dstfd = pfds[1];
    //fd_set_nonblocking (zio->dstfd);

    return (zio);
}

zio_t *zio_writer_create (const char *name, int dstfd, void *arg)
{
    zio_t *zio = zio_allocate (name, 0, arg);
    zio->dstfd = dstfd;
    fd_set_nonblocking (zio->dstfd);

    /*  Return zio object and wait for data via zio_write() operations...
     */
    return (zio);
}

zio_t *zio_pipe_writer_create (const char *name, void *arg)
{
    zio_t *zio;
    int pfds[2];

    if (pipe (pfds) < 0)
        return (NULL);

    if ((zio = zio_writer_create (name, pfds[1], arg)) == NULL) {
        close (pfds[0]);
        close (pfds[1]);
        return (NULL);
    }
    zio->srcfd = pfds[0];
    //fd_set_nonblocking (zio->srcfd);
    return (zio);
}

const char * zio_name (zio_t *zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (NULL);
    return (zio->name);
}

int zio_src_fd (zio_t *zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC)) {
        errno = EINVAL;
        return (-1);
    }
    return (zio->srcfd);
}

int zio_dst_fd (zio_t *zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC)) {
        errno = EINVAL;
        return (-1);
    }
    return (zio->dstfd);
}

int zio_json_decode (const char *json_str, void **pp, bool *eofp)
{
    json_t *o = NULL;
    const char *s_data;
    size_t s_len, len = 0;
    void *data = NULL;
    int eof = 0;

    if (!json_str || !(o = json_loads (json_str, 0, NULL))
                  || json_unpack (o, "{s:b s:s}", "eof", &eof,
                                                  "data", &s_data) < 0) {
        errno = EPROTO;
        goto error;
    }
    if (pp) {
        s_len = strlen (s_data);
        len = BASE64_DECODE_SIZE (s_len);
        data = calloc (1, len);
        if (!data) {
            errno = ENOMEM;
            goto error;
        }
        if (sodium_base642bin (data, len, s_data, s_len,
                               NULL, &len, NULL,
                               sodium_base64_VARIANT_ORIGINAL) < 0) {
            errno = EPROTO;
            goto error;
        }
        *pp = data;
    }
    if (eofp) {
        *eofp = eof;
    }
    json_decref (o);
    return len;
error:
    json_decref (o);
    free (data);
    return -1;
}

char *zio_json_encode (void *data, int len, bool eof)
{
    char *json_str = NULL;
    char *s_data = NULL;
    json_t *o = NULL;
    int s_len;

    s_len = sodium_base64_encoded_len (len, sodium_base64_VARIANT_ORIGINAL);
    s_data = calloc (1, s_len);
    if (!s_data) {
        errno = ENOMEM;
        goto error;
    }
    sodium_bin2base64 (s_data, s_len, data, len,
                       sodium_base64_VARIANT_ORIGINAL);
    if (!(o = json_pack ("{s:b s:s}", "eof", eof, "data", s_data))) {
        errno = ENOMEM;
        goto error;
    }
    if (!(json_str = json_dumps (o, 0))) {
        errno = ENOMEM;
        goto error;
    }
    free (s_data);
    json_decref (o);
    return json_str;
error:
    free (s_data);
    json_decref (o);
    return NULL;
}

bool zio_json_eof (const char *json_str)
{
    bool eof;

    if (zio_json_decode (json_str, NULL, &eof) < 0)
        return false;
    return eof;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
