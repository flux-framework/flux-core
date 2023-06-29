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
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "src/common/libflux/reactor.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"

static const size_t fdwriter_bufsize = 10*1024*1024;

static void fdwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        if ((n = write (fd, buf + count, fdwriter_bufsize - count)) < 0
                                && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: write failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}
static void fdreader (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        if ((n = read (fd, buf + count, fdwriter_bufsize - count)) < 0
                            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: read failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_watcher_stop (w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void test_fd (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *r, *w;

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "fd: successfully created non-blocking socketpair");
    r = flux_fd_watcher_create (reactor, fd[0], FLUX_POLLIN, fdreader, NULL);
    w = flux_fd_watcher_create (reactor, fd[1], FLUX_POLLOUT, fdwriter, NULL);
    ok (r != NULL && w != NULL,
        "fd: reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "fd: reactor ran to completion after %lu bytes", fdwriter_bufsize);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);
    close (fd[0]);
    close (fd[1]);
}

static void buffer_read (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
            "buffer: read from buffer success");

        ok (len == 6,
            "buffer: read returned correct length");

        ok (!memcmp (ptr, "foobar", 6),
            "buffer: read returned correct data");
    }
    else {
        ok (false,
            "buffer: read callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    flux_watcher_stop (w);
    return;
}

static void buffer_read_data_unbuffered (flux_reactor_t *r,
                                         flux_watcher_t *w,
                                         int revents,
                                         void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read_watcher_get_data (w, &len)) != NULL,
            "buffer: read data from buffer success");

        ok (len == 6,
            "buffer: read data returned correct length");

        ok (!memcmp (ptr, "foobar", 6),
            "buffer: read data returned correct data");
    }
    else {
        ok (false,
            "buffer: read callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    flux_watcher_stop (w);
    return;
}


static void buffer_read_line (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read line callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
            "buffer: read line from buffer success");

        ok (len == 4,
            "buffer: read line returned correct length");

        if ((*count) == 0) {
            ok (!memcmp (ptr, "foo\n", 4),
                "buffer: read line returned correct data");
        }
        else {
            ok (!memcmp (ptr, "bar\n", 4),
                "buffer: read line returned correct data");
        }
    }
    else {
        ok (false,
            "buffer: read line callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    if ((*count) == 2)
        flux_watcher_stop (w);
    return;
}

static void buffer_read_data (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read line callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read_watcher_get_data (w, &len)) != NULL,
            "buffer: read data from buffer success");

        ok (len == 4,
            "buffer: read data returned correct length");

        if ((*count) == 0) {
            ok (!memcmp (ptr, "foo\n", 4),
                "buffer: read data returned correct data");
        }
        else {
            ok (!memcmp (ptr, "bar\n", 4),
                "buffer: read data returned correct data");
        }
    }
    else {
        ok (false,
            "buffer: read line callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    if ((*count) == 2)
        flux_watcher_stop (w);
    return;
}


static void buffer_write (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: write callback called with FLUX_POLLERR");
    }
    else {
        ok (flux_buffer_write_watcher_is_closed (w, NULL),
            "buffer: write callback called after close");
    }

    (*count)++;
    flux_watcher_stop (w);
    return;
}

static void buffer_read_fill (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read (fb, 6, &len)) != NULL,
            "buffer: read from buffer success");

        ok (len == 6,
            "buffer: read returned correct length");

        ok (!memcmp (ptr, "foobar", 6),
            "buffer: read returned correct data");
    }
    else {
        ok (false,
            "buffer: read callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    if ((*count) == 3)
        flux_watcher_stop (w);
    return;
}

static void buffer_read_overflow (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer overflow test: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = flux_buffer_read (fb, 6, &len)) != NULL,
            "buffer overflow test: read from buffer success");

        ok (len == 6,
            "buffer overflow test: read returned correct length");

        ok (!memcmp (ptr, "foobar", 6),
            "buffer overflow test: read returned correct data");
    }
    else {
        ok (false,
            "buffer overflow test: read callback failed to return FLUX_POLLIN: %d", revents);
    }

    (*count)++;
    if ((*count) == 3)
        flux_watcher_stop (w);
    return;
}

static void test_buffer (flux_reactor_t *reactor)
{
    int errnum = 0;
    int fd[2];
    int pfds[2];
    flux_watcher_t *w;
    flux_buffer_t *fb;
    int count;
    char buf[1024];

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "buffer: successfully created socketpair");

    /* read buffer test */

    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read,
                                         0,
                                         &count);
    ok (w != NULL,
        "buffer: read created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 1,
        "buffer: read callback successfully called");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* read buffer test with flux_buffer_read_watcher_get_data() */

    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_data_unbuffered,
                                         0,
                                         &count);
    ok (w != NULL,
        "buffer: read created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 1,
        "buffer: read callback successfully called");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);


    /* read line buffer test */

    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_line,
                                         FLUX_WATCHER_LINE_BUFFER,
                                         &count);
    ok (w != NULL,
        "buffer: read line created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (write (fd[1], "foo\nbar\n", 8) == 8,
        "buffer: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 2,
        "buffer: read line callback successfully called twice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* read line with flux_buffer_read_watcher_get_data() */
    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_data,
                                         FLUX_WATCHER_LINE_BUFFER,
                                         &count);
    ok (w != NULL,
        "buffer: read line created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (write (fd[1], "foo\nbar\n", 8) == 8,
        "buffer: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 2,
        "buffer: read line callback successfully called twice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);


    /* write buffer test */

    count = 0;
    w = flux_buffer_write_watcher_create (reactor,
                                          fd[0],
                                          1024,
                                          buffer_write,
                                          0,
                                          &count);
    ok (w != NULL,
        "buffer: write created");

    fb = flux_buffer_write_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    flux_watcher_start (w);

    ok (flux_buffer_write (fb, "bazbar", 6) == 6,
        "buffer: write to buffer success");

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 0,
        "buffer: write callback never called");

    ok (read (fd[1], buf, 1024) == 6,
        "buffer: read from socketpair success");

    ok (!memcmp (buf, "bazbar", 6),
        "buffer: read from socketpair returned correct data");


    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* write buffer test, write to buffer before start */

    count = 0;
    w = flux_buffer_write_watcher_create (reactor,
                                          fd[0],
                                          1024,
                                          buffer_write,
                                          0,
                                          &count);
    ok (w != NULL,
        "buffer: write created");

    fb = flux_buffer_write_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (flux_buffer_write (fb, "foobaz", 6) == 6,
        "buffer: write to buffer success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 0,
        "buffer: write callback never called");

    ok (read (fd[1], buf, 1024) == 6,
        "buffer: read from socketpair success");

    ok (!memcmp (buf, "foobaz", 6),
        "buffer: read from socketpair returned correct data");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* read buffer test, fill buffer before start */

    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         12, /* 12 bytes = 2 "foobars"s */
                                         buffer_read_fill,
                                         0,
                                         &count);
    ok (w != NULL,
        "buffer: read created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (flux_buffer_write (fb, "foobarfoobar", 12) == 12,
        "buffer: write to buffer success");

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 3,
        "buffer: read callback successfully called 3 times");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* read line buffer corner case test - fill buffer to max still works */

    count = 0;
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         12, /* 12 bytes = 2 "foobar"s */
                                         buffer_read_overflow,
                                         0,
                                         &count);
    ok (w != NULL,
        "buffer overflow test: read line created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer overflow test: buffer retrieved");

    ok (write (fd[1], "foobarfoobarfoobar", 18) == 18,
        "buffer overflow test: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer overflow test: reactor ran to completion");

    ok (count == 3,
        "buffer overflow test: read line callback successfully called three times");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* write buffer watcher close() testcase */

    ok (flux_buffer_write_watcher_close (NULL) == -1 && errno == EINVAL,
        "buffer: flux_buffer_write_watcher_close handles NULL argument");

    count = 0;
    ok (pipe (pfds) == 0,
        "buffer: hey I can has a pipe!");

    w = flux_buffer_write_watcher_create (reactor,
                                          pfds[1],
                                          1024,
                                          buffer_write,
                                          0,
                                          &count);
    ok (w == NULL && errno == EINVAL,
        "buffer: write_watcher_create fails with EINVAL if fd !nonblocking");

    ok (fd_set_nonblocking (pfds[1]) >= 0,
        "buffer: fd_set_nonblocking");

    w = flux_buffer_write_watcher_create (reactor,
                                          pfds[1],
                                          1024,
                                          buffer_write,
                                          0,
                                          &count);
    ok (w != NULL,
        "buffer: write watcher close: watcher created");
    fb = flux_buffer_write_watcher_get_buffer (w);
    ok (fb != NULL,
        "buffer: write watcher close: buffer retrieved");

    ok (flux_buffer_write (fb, "foobaz", 6) == 6,
        "buffer: write to buffer success");

    ok (flux_buffer_write_watcher_is_closed (w, NULL) == 0,
        "buffer: flux_buffer_write_watcher_is_closed returns false");
    ok (flux_buffer_write_watcher_close (w) == 0,
        "buffer: flux_buffer_write_watcher_close: Success");
    ok (flux_buffer_write_watcher_is_closed (w, NULL) == 0,
        "buffer: watcher still not closed (close(2) not called yet)");
    ok (flux_buffer_write_watcher_close (w) == -1 && errno == EINPROGRESS,
        "buffer: flux_buffer_write_watcher_close: In progress");

    ok (flux_buffer_write (fb, "shouldfail", 10) == -1 && errno == EROFS,
        "buffer: flux_buffer_write after close fails with EROFS");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 1,
        "buffer: write callback called once");
    ok (flux_buffer_write_watcher_is_closed (w, &errnum) == 1 && errnum == 0,
        "buffer: flux_buffer_write_watcher_is_closed returns true");
    ok (flux_buffer_write_watcher_close (w) == -1 && errno == EINVAL,
        "buffer: flux_buffer_write_watcher_close after close returns EINVAL");

    ok (read (pfds[0], buf, 1024) == 6,
        "buffer: read from pipe success");

    ok (!memcmp (buf, "foobaz", 6),
        "buffer: read from pipe returned correct data");

    ok (read (pfds[0], buf, 1024) == 0,
        "buffer: read from pipe got EOF");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (pfds[0]);
    close (fd[0]);
    close (fd[1]);
}

struct buffer_fd_close
{
    flux_watcher_t *w;
    int count;
    int fd;
};

static void buffer_decref (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct buffer_fd_close *bfc = arg;
    bfc->count++;
    flux_buffer_read_watcher_decref (bfc->w);
    ok (true, "flux_buffer_read_watcher_decref");
    flux_watcher_destroy (w);
}

static void buffer_read_fd_decref (flux_reactor_t *r,
                                   flux_watcher_t *w,
                                   int revents,
                                   void *arg)
{
    struct buffer_fd_close *bfc = arg;
    flux_buffer_t *fb;
    const void *ptr;
    int len;

    if (revents & FLUX_POLLERR) {
        fail ("buffer decref: got FLUX_POLLERR");
        return;
    }
    if (!(revents & FLUX_POLLIN)) {
        fail ("buffer decref: got FLUX_POLLERR");
        return;
    }

    fb = flux_buffer_read_watcher_get_buffer (w);
    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
        "buffer decref: read from buffer success");
    if (!bfc->count) {
        flux_watcher_t *w;
        ok (len == 6,
            "buffer decref: read returned correct length");
        ok (!memcmp (ptr, "foobar", 6),
            "buffer decref: read returned correct data");
        diag ("closing write side of read buffer");
        close (bfc->fd);

        /* Schedule decref of read buffer
         */
        w = flux_timer_watcher_create (r, 0.01, 0., buffer_decref, bfc);
        flux_watcher_start (w);
    }
    else {
        ok (bfc->count == 2,
            "buffer decref: EOF called only after manual decref");
        ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
            "buffer decref: read from buffer success");

        ok (len == 0,
            "buffer decref: read returned 0, socketpair is closed");
        flux_watcher_stop (w);
    }
    bfc->count++;
}

static void buffer_read_fd_close (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg)
{
    struct buffer_fd_close *bfc = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer corner case: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
                "buffer corner case: read from buffer success");

            ok (len == 6,
                "buffer corner case: read returned correct length");

            ok (!memcmp (ptr, "foobar", 6),
                "buffer corner case: read returned correct data");

            close (bfc->fd);
        }
        else {
            ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
                "buffer corner case: read from buffer success");

            ok (len == 0,
                "buffer corner case: read returned 0, socketpair is closed");
        }
    }
    else {
        ok (false,
            "buffer corner case: read callback failed to return FLUX_POLLIN: %d", revents);
    }

    bfc->count++;
    if (bfc->count == 2)
        flux_watcher_stop (w);
    return;
}

static void buffer_read_line_fd_close (flux_reactor_t *r, flux_watcher_t *w,
                                       int revents, void *arg)
{
    struct buffer_fd_close *bfc = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer corner case: read line callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 7,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foobar\n", 7),
                "buffer corner case: read line returned correct data");

            close (bfc->fd);
        }
        else {
            ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 0,
                "buffer corner case: read line returned 0, socketpair is closed");
        }
    }
    else {
        ok (false,
            "buffer corner case: read line callback failed to return FLUX_POLLIN: %d", revents);
    }

    bfc->count++;
    if (bfc->count == 2)
        flux_watcher_stop (w);
    return;
}

static void buffer_read_line_fd_close_and_left_over_data (flux_reactor_t *r,
                                                          flux_watcher_t *w,
                                                          int revents,
                                                          void *arg)
{
    struct buffer_fd_close *bfc = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer corner case: read line callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 7,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foobar\n", 7),
                "buffer corner case: read line returned correct data");

            close (bfc->fd);
        }
        else if (bfc->count == 1) {
            ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 0,
                "buffer corner case: read line says no lines available");

            ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
                "buffer corner case: read from buffer success");

            ok (len == 3,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foo", 3),
                "buffer corner case: read line returned correct data");
        }
        else {
            ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 0,
                "buffer corner case: read line returned 0, socketpair is closed");
        }
    }
    else {
        ok (false,
            "buffer corner case: read line callback failed to return FLUX_POLLIN: %d", revents);
    }

    bfc->count++;
    if (bfc->count == 3)
        flux_watcher_stop (w);
    return;
}

static void test_buffer_refcnt (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *w;
    struct buffer_fd_close bfc;

    /* read buffer decref test - other end closes stream */

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "buffer decref: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_fd_decref,
                                         0,
                                         &bfc);
    ok (w != NULL,
        "buffer decref: read created");
    bfc.w = w;

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer decref: write to socketpair success");

    flux_watcher_start (w);

    diag ("calling flux_buffer_read_watcher_incref");
    flux_buffer_read_watcher_incref (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer decref: reactor ran to completion");

    ok (bfc.count == 3,
        "buffer decref: read callback successfully called thrice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (fd[0]);
}

static void test_buffer_corner_case (flux_reactor_t *reactor)
{
    int fd[2];
    flux_watcher_t *w;
    flux_buffer_t *fb;
    struct buffer_fd_close bfc;

    /* read buffer corner case test - other end closes stream */

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_fd_close,
                                         0,
                                         &bfc);
    ok (w != NULL,
        "buffer corner case: read created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer corner case: buffer retrieved");

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer corner case: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer corner case: reactor ran to completion");

    ok (bfc.count == 2,
        "buffer corner case: read callback successfully called twice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (fd[0]);

    /* read line buffer corner case test - other end closes stream */

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_line_fd_close,
                                         FLUX_WATCHER_LINE_BUFFER,
                                         &bfc);
    ok (w != NULL,
        "buffer corner case: read line created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer corner case: buffer retrieved");

    ok (write (fd[1], "foobar\n", 7) == 7,
        "buffer corner case: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer corner case: reactor ran to completion");

    ok (bfc.count == 2,
        "buffer corner case: read line callback successfully called twice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (fd[0]);

    /* read line buffer corner case test - left over data not a line */

    ok (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = flux_buffer_read_watcher_create (reactor,
                                         fd[0],
                                         1024,
                                         buffer_read_line_fd_close_and_left_over_data,
                                         FLUX_WATCHER_LINE_BUFFER,
                                         &bfc);
    ok (w != NULL,
        "buffer corner case: read line created");

    fb = flux_buffer_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer corner case: buffer retrieved");

    ok (write (fd[1], "foobar\nfoo", 10) == 10,
        "buffer corner case: write to socketpair success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer corner case: reactor ran to completion");

    ok (bfc.count == 3,
        "buffer corner case: read line callback successfully called three times");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (fd[0]);
    close (fd[1]);
}

static int repeat_countdown = 10;
static void repeat (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    repeat_countdown--;
    if (repeat_countdown == 0)
        flux_watcher_stop (w);
}

static int oneshot_runs = 0;
static int oneshot_errno = 0;
static void oneshot (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    oneshot_runs++;
    if (oneshot_errno != 0) {
        errno = oneshot_errno;
        flux_reactor_stop_error (r);
    }
}

static void test_timer (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    double elapsed, t0, t[] = { 0.001, 0.010, 0.050, 0.100, 0.200 };
    int i, rc;

    /* in case this test runs a while after last reactor run.
     */
    flux_reactor_now_update (reactor);

    errno = 0;
    ok (!flux_timer_watcher_create (reactor, -1, 0, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative timeout fails with EINVAL");
    ok (!flux_timer_watcher_create (reactor, 0, -1, oneshot, NULL)
        && errno == EINVAL,
        "timer: creating negative repeat fails with EINVAL");
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating zero timeout oneshot works");
    flux_watcher_start (w);
    oneshot_runs = 0;
    t0 = flux_reactor_now (reactor);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    elapsed = flux_reactor_now (reactor) - t0;
    ok (oneshot_runs == 1,
        "timer: oneshot was executed once (%.3fs)", elapsed);
    oneshot_runs = 0;
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    ok (oneshot_runs == 0,
        "timer: expired oneshot didn't run");

    errno = 0;
    oneshot_errno = ESRCH;
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) < 0 && errno == ESRCH,
        "general: reactor stop_error worked with errno passthru");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_timer_watcher_create (reactor, 0.001, 0.001, repeat, NULL))
        != NULL,
        "timer: creating 1ms timeout with 1ms repeat works");
    flux_watcher_start (w);
    repeat_countdown = 10;
    t0 = flux_reactor_now (reactor);
    ok (flux_reactor_run (reactor, 0) == 0,
        "timer: reactor exited normally");
    elapsed = flux_reactor_now (reactor) - t0;
    ok (repeat_countdown == 0,
        "timer: repeat timer ran 10x and stopped itself");
    ok (elapsed >= 0.001*10,
        "timer: elapsed time is >= 10*1ms (%.3fs)", elapsed);
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    oneshot_errno = 0;
    ok ((w = flux_timer_watcher_create (reactor, 0, 0, oneshot, NULL)) != NULL,
        "timer: creating timer watcher works");
    for (i = 0; i < ARRAY_SIZE (t); i++) {
        flux_timer_watcher_reset (w, t[i], 0);
        flux_watcher_start (w);
        t0 = flux_reactor_now (reactor);
        oneshot_runs = 0;
        rc = flux_reactor_run (reactor, 0);
        elapsed = flux_reactor_now (reactor) - t0;
        ok (rc == 0 && oneshot_runs == 1 && elapsed >= t[i],
            "timer: reactor ran %.3fs oneshot at >= time (%.3fs)", t[i], elapsed);
    }
    flux_watcher_destroy (w);
}


/* A reactor callback that immediately stops reactor without error */
static bool do_stop_callback_ran = false;
static void do_stop_reactor (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    do_stop_callback_ran = true;
    flux_reactor_stop (r);
}

double time_now ()
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0) {
        fprintf (stderr, "clock_gettime: %s\n", strerror (errno));
        return -1.;
    }
    return ts.tv_sec + ts.tv_nsec/1.e9;
}

/* Periodic watcher "reschedule callback* */
static bool resched_called = false;
static double resched_cb (flux_watcher_t *w, double now, void *arg)
{
    flux_reactor_t *r = arg;
    ok (r != NULL, "resched callback called with proper arg");
    resched_called = true;
    return (now + .1);
}

static double resched_cb_negative (flux_watcher_t *w, double now, void *arg)
{
    return (now - 100.);
}

/*  These tests exercise most basic functionality of periodic watchers,
 *   but we're not able to fully test whether periodic watcher respects
 *   time jumps (as described in ev(7) man page) with these simple
 *   tests.
 */
static void test_periodic (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    errno = 0;
    oneshot_errno = 0;
    ok (!flux_periodic_watcher_create (reactor, -1, 0, NULL, oneshot, NULL)
        && errno == EINVAL,
        "periodic: creating negative offset fails with EINVAL");
    ok (!flux_periodic_watcher_create (reactor, 0, -1, NULL, oneshot, NULL)
        && errno == EINVAL,
        "periodic: creating negative interval fails with EINVAL");
    ok ((w = flux_periodic_watcher_create (reactor, 0, 0, NULL, oneshot, NULL))
        != NULL,
        "periodic: creating zero offset/interval works");
    flux_watcher_start (w);
    oneshot_runs = 0;
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor ran to completion");
    ok (oneshot_runs == 1,
        "periodic: oneshot was executed once");
    oneshot_runs = 0;
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    repeat_countdown = 5;
    ok ((w = flux_periodic_watcher_create (reactor, 0.01, 0.01,
                                           NULL, repeat, NULL)) != NULL,
        "periodic: creating 10ms interval works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor ran to completion");
    ok (repeat_countdown == 0, "repeat ran for expected number of times");
    oneshot_runs = 0;

    /* test reset */
    flux_periodic_watcher_reset (w, time_now () + 123., 0, NULL);
    /* Give 1s error range, time may march forward between reset and now */
    diag ("next wakeup = %.2f, now + offset = %.2f",
          flux_watcher_next_wakeup (w), time_now () + 123.);
    ok (fabs (flux_watcher_next_wakeup (w) - (time_now () + 123.)) <= .5,
        "flux_periodic_watcher_reset works");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    ok ((w = flux_periodic_watcher_create (reactor, 0, 0, resched_cb,
                                           do_stop_reactor, reactor)) != NULL,
        "periodic: creating with resched callback works");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) >= 0,
        "periodic: reactor ran to completion");
    ok (resched_called, "resched_cb was called");
    ok (do_stop_callback_ran, "stop reactor callback was run");
    oneshot_runs = 0;
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    do_stop_callback_ran = false;
    ok ((w = flux_periodic_watcher_create (reactor, 0, 0, resched_cb_negative,
                                           do_stop_reactor, reactor)) != NULL,
        "periodic: create watcher with misconfigured resched callback");
    flux_watcher_start (w);
    ok (flux_reactor_run (reactor, 0) == 0,
        "periodic: reactor stopped immediately");
    ok (do_stop_callback_ran == false, "periodic: callback did not run");
    flux_watcher_stop (w);
    flux_watcher_destroy (w);

}

static int idle_count = 0;
static void idle_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    if (++idle_count == 42)
        flux_watcher_stop (w);
}

static void test_idle (flux_reactor_t *reactor)
{
    flux_watcher_t *w;

    w = flux_idle_watcher_create (reactor, idle_cb, NULL);
    ok (w != NULL,
        "created idle watcher");
    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");
    ok (idle_count == 42,
        "idle watcher ran until stopped");
    flux_watcher_destroy (w);
}

static int prepare_count = 0;
static void prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    prepare_count++;
}

static int check_count = 0;
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    check_count++;
}

static int prepchecktimer_count = 0;
static void prepchecktimer_cb (flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg)
{
    if (++prepchecktimer_count == 8)
        flux_reactor_stop (r);
}

static void test_prepcheck (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *prep;
    flux_watcher_t *chk;

    w = flux_timer_watcher_create (reactor, 0.01, 0.01,
                                   prepchecktimer_cb, NULL);
    ok (w != NULL,
        "created timer watcher that fires every 0.01s");
    flux_watcher_start (w);

    prep = flux_prepare_watcher_create (reactor, prepare_cb, NULL);
    ok (w != NULL,
        "created prepare watcher");
    flux_watcher_start (prep);

    chk = flux_check_watcher_create (reactor, check_cb, NULL);
    ok (w != NULL,
        "created check watcher");
    flux_watcher_start (chk);

    ok (flux_reactor_run (reactor, 0) >= 0,
        "reactor ran successfully");
    ok (prepchecktimer_count == 8,
        "timer fired 8 times, then reactor was stopped");
    diag ("prep %d check %d timer %d", prepare_count, check_count,
                                       prepchecktimer_count);
    ok (prepare_count >= 8,
        "prepare watcher ran at least once per timer");
    ok (check_count >= 8,
        "check watcher ran at least once per timer");

    flux_watcher_destroy (w);
    flux_watcher_destroy (prep);
    flux_watcher_destroy (chk);
}

static int sigusr1_count = 0;
static void sigusr1_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    if (++sigusr1_count == 8)
        flux_reactor_stop (r);
}

static void sigidle_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    if (kill (getpid (), SIGUSR1) < 0)
        flux_reactor_stop_error (r);
}

static void test_signal (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_watcher_t *idle;

    w = flux_signal_watcher_create (reactor, SIGUSR1, sigusr1_cb, NULL);
    ok (w != NULL,
        "created signal watcher");
    flux_watcher_start (w);

    idle = flux_idle_watcher_create (reactor, sigidle_cb, NULL);
    ok (idle != NULL,
        "created idle watcher");
    flux_watcher_start (idle);

    ok (flux_reactor_run (reactor, 0) >= 0,
        "reactor ran successfully");
    ok (sigusr1_count == 8,
        "signal watcher handled correct number of SIGUSR1's");

    flux_watcher_destroy (w);
    flux_watcher_destroy (idle);
}

static pid_t child_pid = -1;
static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    int pid = flux_child_watcher_get_rpid (w);
    int rstatus = flux_child_watcher_get_rstatus (w);
    ok (pid == child_pid,
        "child watcher called with expected rpid");
    ok (WIFSIGNALED (rstatus) && WTERMSIG (rstatus) == SIGHUP,
        "child watcher called with expected rstatus");
    flux_watcher_stop (w);
}

static void test_child  (flux_reactor_t *reactor)
{
    flux_watcher_t *w;
    flux_reactor_t *r;

    child_pid = fork ();
    if (child_pid == 0) {
        pause ();
        exit (0);
    }
    errno = 0;
    w = flux_child_watcher_create (reactor, child_pid, false, child_cb, NULL);
    ok (w == NULL && errno == EINVAL,
        "child watcher failed with EINVAL on non-SIGCHLD reactor");
    ok ((r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)) != NULL,
        "created reactor with SIGCHLD flag");
    w = flux_child_watcher_create (r, child_pid, false, child_cb, NULL);
    ok (w != NULL,
        "created child watcher");

    ok (kill (child_pid, SIGHUP) == 0,
        "sent child SIGHUP");
    flux_watcher_start (w);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran successfully");
    flux_watcher_destroy (w);
    flux_reactor_destroy (r);
}

struct stat_ctx {
    int fd;
    char *path;
    int stat_size;
    int stat_nlink;
    enum { STAT_APPEND, STAT_WAIT, STAT_UNLINK } state;
};
static void stat_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct stat_ctx *ctx = arg;
    struct stat new, old;
    flux_stat_watcher_get_rstat (w, &new, &old);
    if (new.st_nlink == 0) {
        diag ("%s: nlink: old: %d new %d", __FUNCTION__,
                old.st_nlink, new.st_nlink);
        ctx->stat_nlink++;
        flux_watcher_stop (w);
    } else {
        if (old.st_size != new.st_size) {
            diag ("%s: size: old=%ld new=%ld", __FUNCTION__,
                  (long)old.st_size, (long)new.st_size);
            ctx->stat_size++;
            ctx->state = STAT_UNLINK;
        }
    }
}

static void stattimer_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    struct stat_ctx *ctx = arg;
    if (ctx->state == STAT_APPEND) {
        if (write (ctx->fd, "hello\n", 6) < 0 || close (ctx->fd) < 0)
            flux_reactor_stop_error (r);
        ctx->state = STAT_WAIT;
    } else if (ctx->state == STAT_UNLINK) {
        if (unlink (ctx->path) < 0)
            flux_reactor_stop_error (r);
        flux_watcher_stop (w);
    }
}

static void test_stat (flux_reactor_t *reactor)
{
    flux_watcher_t *w, *tw;
    struct stat_ctx ctx = {0};
    const char *tmpdir = getenv ("TMPDIR");

    ctx.path = xasprintf ("%s/reactor-test.XXXXXX", tmpdir ? tmpdir : "/tmp");
    ctx.fd = mkstemp (ctx.path);
    ctx.state = STAT_APPEND;

    ok (ctx.fd >= 0,
        "created temporary file");
    w = flux_stat_watcher_create (reactor, ctx.path, 0., stat_cb, &ctx);
    ok (w != NULL,
        "created stat watcher");
    flux_watcher_start (w);

    tw = flux_timer_watcher_create (reactor, 0.01, 0.01,
                                    stattimer_cb, &ctx);
    ok (tw != NULL,
        "created timer watcher");
    flux_watcher_start (tw);

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran successfully");

    ok (ctx.stat_size == 1,
        "stat watcher invoked once for size change");
    ok (ctx.stat_nlink == 1,
        "stat watcher invoked once for nlink set to zero");

    flux_watcher_destroy (w);
    flux_watcher_destroy (tw);
    free (ctx.path);
}

static void active_idle_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    int *count = arg;
    (*count)++;

    if (*count >= 16)
        flux_reactor_stop_error (r);
}


static void test_active_ref (flux_reactor_t *r)
{
    flux_watcher_t *w;
    int count;

    ok (flux_reactor_run (r, 0) == 0,
        "flux_reactor_run with no watchers returned immediately");

    if (!(w = flux_idle_watcher_create (r, active_idle_cb, &count)))
        BAIL_OUT ("flux_idle_watcher_create failed");
    flux_watcher_start (w);

    count = 0;
    ok (flux_reactor_run (r, 0) < 0 && count == 16,
        "flux_reactor_run with one watcher stopped after 16 iterations");

    flux_reactor_active_decref (r);

    count = 0;
    ok (flux_reactor_run (r, 0) == 0 && count == 1,
        "flux_reactor_run with one watcher+decref returned after 1 iteration");

    flux_reactor_active_incref (r);

    count = 0;
    ok (flux_reactor_run (r, 0) < 0 && count == 16,
        "flux_reactor_run with one watcher+incref stopped after 16 iterations");

    flux_watcher_destroy (w);
}

static void reactor_destroy_early (void)
{
    flux_reactor_t *r;
    flux_watcher_t *w;

    if (!(r = flux_reactor_create (0)))
        exit (1);
    if (!(w = flux_idle_watcher_create (r, NULL, NULL)))
        exit (1);
    flux_watcher_start (w);
    flux_reactor_destroy (r);
    flux_watcher_destroy (w);
}

static void test_reactor_flags (flux_reactor_t *r)
{
    errno = 0;
    ok (flux_reactor_run (r, 0xffff) < 0 && errno == EINVAL,
        "flux_reactor_run flags=0xffff fails with EINVAL");

    errno = 0;
    ok (flux_reactor_create (0xffff) == NULL && errno == EINVAL,
        "flux_reactor_create flags=0xffff fails with EINVAL");
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor ran to completion (no watchers)");

    test_timer (reactor);
    test_periodic (reactor);
    test_fd (reactor);
    test_buffer (reactor);
    test_buffer_refcnt (reactor);
    test_buffer_corner_case (reactor);
    test_idle (reactor);
    test_prepcheck (reactor);
    test_signal (reactor);
    test_child (reactor);
    test_stat (reactor);
    test_active_ref (reactor);
    test_reactor_flags (reactor);

    flux_reactor_destroy (reactor);

    lives_ok ({ reactor_destroy_early ();},
        "destroying reactor then watcher doesn't segfault");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

