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
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <czmq.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/liblsd/cbuf.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"


#ifndef NDEBUG
#  define ZIO_MAGIC         0x510015
#endif

#define ZIO_EOF             (1<<0)
#define ZIO_EOF_SENT        (1<<1)
#define ZIO_BUFFERED        (1<<2)
#define ZIO_LINE_BUFFERED   (1<<4)
#define ZIO_CLOSED          (1<<5)
#define ZIO_VERBOSE         (1<<6)

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
    void *     dstsock; /*  ZMQ dst socket                                   */
    cbuf_t     buf;     /*  Buffer for I/O (if needed)                       */
    size_t     buffersize;

    unsigned   flags;   /*  Flags for state and options of zio object        */

    zio_send_f send;    /*  Callback to send json data                       */
    zio_close_f close;  /*  Callback after eof is sent                       */

    zloop_t    *zloop;  /*  zloop if we are connected to one                 */
    flux_t      flux;   /*  flux handle if we are using flux reactor         */
    void *arg;          /*  Arg passed to callbacks                          */
};

static void zio_vlog (zio_t zio, const char *format, va_list ap)
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

static int zio_verbose (zio_t zio)
{
    return (zio->flags & ZIO_VERBOSE);
}

static void zio_debug (zio_t zio, const char *fmt, ...)
{
    if (zio_verbose (zio)) {
        va_list ap;
        va_start (ap, fmt);
        zio_vlog (zio, fmt, ap);
        va_end (ap);
    }
}

static int fd_set_nonblocking (int fd)
{
    int fval;

    assert (fd >= 0);

    if ((fval = fcntl (fd, F_GETFL, 0)) < 0)
        return (-1);
    if (fcntl (fd, F_SETFL, fval | O_NONBLOCK) < 0)
        return (-1);
    return (0);
}

void zio_destroy (zio_t z)
{
    if (z == NULL)
        return;
    assert (z->magic == ZIO_MAGIC);
    if (z->buf)
        cbuf_destroy (z->buf);
    free (z->name);
    free (z->prefix);
    z->srcfd = z->dstfd = -1;
    assert (z->magic = ~ZIO_MAGIC);
    free (z);
}

static int zio_init_buffer (zio_t zio)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    assert (zio->buf == NULL);

    if (!(zio->buf = cbuf_create (64, 1638400)))
        return (-1);

    cbuf_opt_set (zio->buf, CBUF_OPT_OVERWRITE, CBUF_NO_DROP);
    return (0);
}


static zio_t zio_allocate (const char *name, int reader, void *arg)
{
    zio_t z;

    if (!name) {
        errno = EINVAL;
        return (NULL);
    }

    if (!(z = malloc (sizeof (*z))))
        return NULL;

    memset (z, 0, sizeof (*z));
    assert (z->magic = ZIO_MAGIC);

    if (!(z->name = strdup (name))) {
        zio_destroy (z);
        return (NULL);
    }

    z->arg = arg;
    z->io_type = reader ? ZIO_READER : ZIO_WRITER;
    z->flags = ZIO_BUFFERED | ZIO_LINE_BUFFERED;
    z->buffersize = 4096;

    z->srcfd = z->dstfd = -1;
    z->prefix = NULL;

    zio_init_buffer (z);

    return (z);
}

/*
 *  zio reader reads from srcfd and generates json data to sendfn
 */
int zio_reader (zio_t zio)
{
    return (zio->io_type == ZIO_READER);
}

/*
 *  zio writer consumes data in json and sends to dstfd
 */
int zio_writer (zio_t zio)
{
    return (zio->io_type == ZIO_WRITER);
}

static inline void zio_clear_buffered (zio_t zio)
{
    zio->flags &= ~(ZIO_LINE_BUFFERED | ZIO_BUFFERED);
}

static inline int zio_line_buffered (zio_t zio)
{
    return (zio->flags & ZIO_LINE_BUFFERED);
}

static inline int zio_buffered (zio_t zio)
{
    return (zio->flags & ZIO_BUFFERED);
}


static inline void zio_set_eof (zio_t zio)
{
    zio->flags |= ZIO_EOF;
}

static inline int zio_eof (zio_t zio)
{
    return (zio->flags & ZIO_EOF);
}


static int zio_eof_pending (zio_t zio)
{
    /*
     *   zio object has EOF pending if EOF flag is set and either the
     *    io is unbuffered or the buffer for IO is empty.
     */
    return (zio_eof (zio) && (!cbuf_used (zio->buf)));
}

static int zio_buffer_used (zio_t zio)
{
    return cbuf_used (zio->buf);
}

static int zio_buffer_empty (zio_t zio)
{
    return (!zio_buffered (zio) || (cbuf_used (zio->buf) == 0));
}

static int zio_eof_sent (zio_t zio)
{
    return (zio->flags & ZIO_EOF_SENT);
}

int zio_set_unbuffered (zio_t zio)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);

    zio_clear_buffered (zio);
    if (zio->buf) {
        /* XXX: Drain buffer or set it to drain, then destroy */
    }
    return (0);
}

int zio_set_buffered (zio_t zio, size_t buffersize)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    zio->flags |= ZIO_BUFFERED;
    if (!zio->buf)
        return zio_init_buffer (zio);
    return (0);
}

int zio_set_line_buffered (zio_t zio)
{
    int rc = zio_set_buffered (zio, 4096);
    zio->flags |= ZIO_LINE_BUFFERED;
    return (rc);
}

static int zio_set_verbose (zio_t zio)
{
    zio->flags |= ZIO_VERBOSE;
    return (0);
}

int zio_set_quiet (zio_t zio)
{
    zio->flags &= (~ZIO_VERBOSE);
    return (0);
}

int zio_set_debug (zio_t zio, const char *prefix, zio_log_f logf)
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

int zio_set_send_cb (zio_t zio, zio_send_f sendf)
{
    zio->send = sendf;
    return (0);
}

int zio_set_close_cb (zio_t zio, zio_close_f closef)
{
    zio->close = closef;
    return (0);
}

static int zio_read (zio_t zio, void *dst, int len)
{
    assert (zio != NULL);
    assert (zio->magic == ZIO_MAGIC);
    assert (zio->buf);

    if (zio_line_buffered (zio) && !zio_eof (zio))
        return cbuf_read_line (zio->buf, dst, len, -1);
    else
        return cbuf_read (zio->buf, dst, len);
}

static json_object * zio_json_object_create (zio_t zio, void *data, size_t len)
{
    bool eof = false;
    if (zio_eof_pending (zio)) {
        eof = true;
        zio_debug (zio, "Setting EOF sent\n");
        zio->flags |= ZIO_EOF_SENT;
    }
    return zio_json_encode (data, len, eof);
}

static int zio_sendmsg (zio_t zio, json_object *o)
{
    zio_debug (zio, "sendmsg: %s\n", json_object_to_json_string (o));
    return (*zio->send) (zio, o, zio->arg);
}

static int zio_send (zio_t zio, char *p, size_t len)
{
    int rc;
    zio_debug (zio, "zio_send (len=%d)\n", len);
    json_object *o = zio_json_object_create (zio, p, len);
    rc = zio_sendmsg (zio, o);
    json_object_put (o);
    return rc;
}

static int zio_data_to_flush (zio_t zio)
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

int zio_closed (zio_t zio)
{
    return (zio->flags & ZIO_CLOSED);
}

static int zio_close (zio_t zio)
{
    zio_debug (zio, "zio_close\n");
    if (zio_reader (zio)) {
        close (zio->srcfd);
        zio->srcfd = -1;
    }
    else if (zio_writer (zio)) {
        close (zio->dstfd);
        zio->dstfd = -1;
    }
    zio->flags |= ZIO_CLOSED;
    if (zio->close)
        return (*zio->close) (zio, zio->arg);

    return (0);
}


/*
 *   Flush any buffered output and EOF from zio READER object
 *    to destination.
 */
int zio_flush (zio_t zio)
{
    int len;
    int rc = 0;

    if ((zio == NULL) || (zio->magic != ZIO_MAGIC) || !(zio->send))
        return (-1);

    /*
     *  Nothing to flush if EOF already sent to consumer:
     */
    if (zio_eof_sent (zio))
        return (0);

    while (((len = zio_data_to_flush (zio)) > 0) || zio_eof (zio)) {
        char * buf = NULL;
        int n = 0;
        zio_debug (zio, "zio_flush: len = %d, eof = %d\n", len, zio_eof (zio));
        if (len > 0) {
            buf = xzmalloc (len + 1);
            if ((n = zio_read (zio, buf, len + 1)) <= 0) {
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

static int zio_read_cb_common (zio_t zio)
{
    int n;
    if ((n = cbuf_write_from_fd (zio->buf, zio->srcfd, -1, NULL)) < 0) {
        if (errno == EAGAIN);
            return (0);
        zio_debug (zio, "read: %s\n", strerror (errno));
        return (-1);
    }

    zio_debug (zio, "zio_read_cb: read = %d\n", n);

    if (n == 0) {
        zio_set_eof (zio);
        zio_debug (zio, "zio_read_cb: Got eof\n");
    }

    zio_flush (zio);

    return (0);
}

static int zio_zloop_read_cb (zloop_t *zl, zmq_pollitem_t *zp, zio_t zio)
{
    if (zio_read_cb_common (zio) < 0)
        return (-1);

    if (zio_eof_sent (zio)) {
        zio_debug (zio, "reader detaching from zloop\n");
        zloop_poller_end (zl, zp);
        return (zio_close (zio));
    }
    return (0);
}

static int zio_flux_read_cb (flux_t f, int fd, short revents, zio_t zio)
{
    if (zio_read_cb_common (zio) < 0)
        return (-1);

    if (zio_eof_sent (zio)) {
        zio_debug (zio, "reader detaching from flux reactor\n");
        flux_fdhandler_remove (f, fd, ZMQ_POLLIN|ZMQ_POLLERR);
        return (zio_close (zio));
    }
    return (0);
}

static int zio_write_pending (zio_t zio)
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
static int zio_writer_cb (zio_t zio)
{
    int rc = cbuf_read_to_fd (zio->buf, zio->dstfd, -1);
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

static int zio_zloop_writer_cb (zloop_t *zl, zmq_pollitem_t *zp, zio_t zio)
{
    int rc = zio_writer_cb (zio);
    if (!zio_write_pending (zio))
        zloop_poller_end (zl, zp);
    return (rc);
}

static int zio_flux_writer_cb (flux_t f, int fd, short revents, zio_t zio)
{
    int rc = zio_writer_cb (zio);
    if (!zio_write_pending (zio))
        flux_fdhandler_remove (f, fd, ZMQ_POLLOUT | ZMQ_POLLERR);
    return (rc);
}

static int zio_zloop_reader_poll (zio_t zio)
{
    zmq_pollitem_t zp = { .fd = zio->srcfd,
                          .events = ZMQ_POLLIN | ZMQ_POLLERR,
                          .socket = NULL };
#ifdef ZMQ_IGNERR
    zp.events |= ZMQ_IGNERR;
#endif
    zloop_poller (zio->zloop, &zp,
        (zloop_fn *) zio_zloop_read_cb, (void *) zio);
#ifndef ZMQ_IGNERR
    zloop_set_tolerant (zio->zloop, &zp);
#endif
    return (0);
}

/* Note: flux reactor sets ZMQ_IGNERR/zloop_set_tolerant as default in fd_add.
 */
static int zio_flux_reader_poll (zio_t zio)
{
    if (!zio->flux)
        return (-1);
    return flux_fdhandler_add (zio->flux, zio->srcfd,
            ZMQ_POLLIN | ZMQ_POLLERR,
            (FluxFdHandler) &zio_flux_read_cb,
            (void *) zio);
}

static int zio_reader_poll (zio_t zio)
{
    if (zio->zloop)
        return zio_zloop_reader_poll (zio);
    else if (zio->flux)
        return zio_flux_reader_poll (zio);
    return (-1);
}

/*
 *  Schedule pending data to write to zio->dstfd
 */
static int zio_zloop_writer_schedule (zio_t zio)
{
    zmq_pollitem_t zp = { .fd = zio->dstfd,
                          .events = ZMQ_POLLOUT | ZMQ_POLLERR,
                          .socket = NULL };
    if (!zio->zloop)
        return (-1);

    return zloop_poller (zio->zloop, &zp,
        (zloop_fn *) zio_zloop_writer_cb, (void *) zio);
}

static int zio_flux_writer_schedule (zio_t zio)
{
    if (!zio->flux)
        return (-1);
    return flux_fdhandler_add (zio->flux, zio->dstfd,
            ZMQ_POLLOUT | ZMQ_POLLERR,
            (FluxFdHandler) zio_flux_writer_cb,
            (void *) zio);
}

static int zio_writer_schedule (zio_t zio)
{
    if (zio->zloop)
        return zio_zloop_writer_schedule (zio);
    else if (zio->flux)
        return zio_flux_writer_schedule (zio);
    return (-1);
}

/*
 *  write data into zio buffer
 */
static int zio_write_data (zio_t zio, char *buf, size_t len)
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

/*
 *  Write json object to this zio object, buffering unwritten data.
 */
int zio_write_json (zio_t zio, json_object *o)
{
    char *s;
    int len, rc = 0;
    bool eof;

    if ((zio == NULL) || (zio->magic != ZIO_MAGIC) || !zio_writer (zio)) {
        errno = EINVAL;
        return (-1);
    }
    len = zio_json_decode (o, (void **)&s, &eof);
    if (len < 0) {
        errno = EINVAL;
        return (-1);
    }
    if (eof)
        zio_set_eof (zio);
    if (len > 0) {
        rc = zio_write_data (zio, s, len);
        free (s);
    }

    zio_debug (zio, "zio_write: %d bytes, eof=%d\n", len, zio_eof (zio));

    if (zio_write_pending (zio))
        zio_writer_schedule (zio);

    return (rc);
}

static int zio_bootstrap (zio_t zio)
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

int zio_zloop_attach (zio_t zio, zloop_t *zloop)
{
    errno = EINVAL;
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (-1);

    zio->zloop = zloop;
    return (zio_bootstrap (zio));
}

int zio_flux_attach (zio_t zio, flux_t f)
{
    errno = EINVAL;
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (-1);

    zio->flux = f;
    return (zio_bootstrap (zio));
}

int zio_zmsg_send (zio_t zio, json_object *o, void *arg)
{
    const char *s;
    zio_debug (zio, "%s: send: %s\n", zio->name, json_object_to_json_string (o));
    if (!zio->dstsock)
        return (-1);
    zmsg_t *zmsg = zmsg_new ();
    s = json_object_to_json_string (o);
    zmsg_pushstr (zmsg, s);
    zmsg_pushstr (zmsg, zio_name (zio));
    return (zmsg_send (&zmsg, zio->dstsock));
}

zio_t zio_reader_create (const char *name, int srcfd, void *dst, void *arg)
{
    zio_t zio = zio_allocate (name, 1, arg);

    zio->srcfd = srcfd;
    fd_set_nonblocking (zio->srcfd);
    zio->dstsock = dst;
    zio->send = (zio_send_f) zio_zmsg_send;
    return (zio);
}

zio_t zio_pipe_reader_create (const char *name, void *dst, void *arg)
{
    zio_t zio;
    int pfds[2];

    if (pipe (pfds) < 0)
        return (NULL);

    if ((zio = zio_reader_create (name, pfds[0], dst, arg)) == NULL) {
        close (pfds[0]);
        close (pfds[1]);
        return (NULL);
    }
    zio->dstfd = pfds[1];
    //fd_set_nonblocking (zio->dstfd);

    return (zio);
}

zio_t zio_writer_create (const char *name, int dstfd, void *arg)
{
    zio_t zio = zio_allocate (name, 0, arg);
    zio->dstfd = dstfd;
    fd_set_nonblocking (zio->dstfd);

    /*  Return zio object and wait for data via zio_write() operations...
     */
    return (zio);
}

zio_t zio_pipe_writer_create (const char *name, void *arg)
{
    zio_t zio;
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

const char * zio_name (zio_t zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC))
        return (NULL);
    return (zio->name);
}

int zio_src_fd (zio_t zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC)) {
        errno = EINVAL;
        return (-1);
    }
    return (zio->srcfd);
}

int zio_dst_fd (zio_t zio)
{
    if ((zio == NULL) || (zio->magic != ZIO_MAGIC)) {
        errno = EINVAL;
        return (-1);
    }
    return (zio->dstfd);
}

int zio_json_decode (json_object *o, void **pp, bool *eofp)
{
    int len, rc = -1;

    if (o) {
        if (util_json_object_get_boolean (o, "eof", eofp) == 0)
            rc = 0;
        if (util_json_object_get_data (o, "data", (uint8_t **) pp, &len) == 0)
            rc = len; 
    }
    return rc;
}

json_object *zio_json_encode (void *p, int len, bool eof)
{
    json_object *o = util_json_object_new_object ();

    if (len && p)
        util_json_object_add_data (o, "data", (uint8_t *) p, len);
    if (eof)
        util_json_object_add_boolean (o, "eof", 1);
    return (o);
}

bool zio_json_eof (json_object *o)
{
    bool eof = false;
    (void)util_json_object_get_boolean (o, "eof", &eof);
    return eof;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
