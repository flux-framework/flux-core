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
#include <sys/types.h>
#include <sys/socket.h>
#include <flux/core.h>

#include "src/common/libutil/fdutils.h"
#include "src/common/libtap/tap.h"

#include "fbuf_watcher.h"

static void buffer_read (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    int *count = arg;

    if (revents & FLUX_POLLERR) {
        ok (false,
            "buffer: read callback incorrectly called with FLUX_POLLERR");
    }
    else if (revents & FLUX_POLLIN) {
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
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

        ok ((ptr = fbuf_read_watcher_get_data (w, &len)) != NULL,
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
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

        ok ((ptr = fbuf_read_watcher_get_data (w, &len)) != NULL,
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
        /* First callback is so user knows initial buffer size */
        if ((*count) == 0) {
            struct fbuf *fb = fbuf_write_watcher_get_buffer (w);
            int space = fbuf_size (fb);
            ok (space == 1024,
                "buffer: write callback gets correct buffer size");
        }
        /* Second callback is when space is reclaimed */
        else if ((*count) == 1) {
            struct fbuf *fb = fbuf_write_watcher_get_buffer (w);
            int space = fbuf_space (fb);
            ok (space == 1024,
                "buffer: write callback gets correct amount of space");
        }
        else {
            ok (fbuf_write_watcher_is_closed (w, NULL),
                "buffer: write callback called after close");
        }
    }

    (*count)++;
    if ((*count) == 1)
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = fbuf_read (fb, 6, &len)) != NULL,
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        ok ((ptr = fbuf_read (fb, 6, &len)) != NULL,
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

int create_socketpair_nonblock (int *fd)
{
#ifdef SOCK_NONBLOCK
    if (socketpair (PF_LOCAL, SOCK_STREAM|SOCK_NONBLOCK, 0, fd) < 0)
        return -1;
#else
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) < 0
        || fd_set_nonblocking (fd[0]) < 0
        || fd_set_nonblocking (fd[1]) < 0)
        return -1;
#endif
    return 0;
}

static void test_buffer (flux_reactor_t *reactor)
{
    int errnum = 0;
    int fd[2];
    int pfds[2];
    flux_watcher_t *w;
    struct fbuf *fb;
    int count;
    char buf[1024];

    ok (create_socketpair_nonblock (fd) == 0,
        "buffer: successfully created socketpair");

    /* read buffer test */

    count = 0;
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read,
                                  0,
                                  &count);
    ok (w != NULL,
        "buffer: read created");

    fb = fbuf_read_watcher_get_buffer (w);

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

    /* read buffer test with fbuf_read_watcher_get_data() */

    count = 0;
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_data_unbuffered,
                                  0,
                                  &count);
    ok (w != NULL,
        "buffer: read created");

    fb = fbuf_read_watcher_get_buffer (w);

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
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_line,
                                  FBUF_WATCHER_LINE_BUFFER,
                                  &count);
    ok (w != NULL,
        "buffer: read line created");

    fb = fbuf_read_watcher_get_buffer (w);

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

    /* read line with fbuf_read_watcher_get_data() */
    count = 0;
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_data,
                                  FBUF_WATCHER_LINE_BUFFER,
                                  &count);
    ok (w != NULL,
        "buffer: read line created");

    fb = fbuf_read_watcher_get_buffer (w);

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
    w = fbuf_write_watcher_create (reactor,
                                   fd[0],
                                   1024,
                                   buffer_write,
                                   0,
                                   &count);
    ok (w != NULL,
        "buffer: write created");

    ok (flux_watcher_is_active (w) == false,
        "flux_watcher_is_active() returns false on write buffer after create");

    fb = fbuf_write_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    flux_watcher_start (w);

    ok (flux_watcher_is_active (w) == true,
        "flux_watcher_is_active() returns true on write buffer after start");

    ok (fbuf_write (fb, "bazbar", 6) == 6,
        "buffer: write to buffer success");

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 2,
        "buffer: write callback called 2 times");

    ok (read (fd[1], buf, 1024) == 6,
        "buffer: read from socketpair success");

    ok (!memcmp (buf, "bazbar", 6),
        "buffer: read from socketpair returned correct data");


    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* write buffer test, write to buffer before start */

    count = 0;
    w = fbuf_write_watcher_create (reactor,
                                   fd[0],
                                   1024,
                                   buffer_write,
                                   0,
                                   &count);
    ok (w != NULL,
        "buffer: write created");

    fb = fbuf_write_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (fbuf_write (fb, "foobaz", 6) == 6,
        "buffer: write to buffer success");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 2,
        "buffer: write callback called 2 times");

    ok (read (fd[1], buf, 1024) == 6,
        "buffer: read from socketpair success");

    ok (!memcmp (buf, "foobaz", 6),
        "buffer: read from socketpair returned correct data");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    /* read buffer test, fill buffer before start */

    count = 0;
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  12, /* 12 bytes = 2 "foobars"s */
                                  buffer_read_fill,
                                  0,
                                  &count);
    ok (w != NULL,
        "buffer: read created");

    fb = fbuf_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer: buffer retrieved");

    ok (fbuf_write (fb, "foobarfoobar", 12) == 12,
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
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  12, /* 12 bytes = 2 "foobar"s */
                                  buffer_read_overflow,
                                  0,
                                  &count);
    ok (w != NULL,
        "buffer overflow test: read line created");

    fb = fbuf_read_watcher_get_buffer (w);

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

    ok (fbuf_write_watcher_close (NULL) == -1 && errno == EINVAL,
        "buffer: fbuf_write_watcher_close handles NULL argument");

    count = 0;
    ok (pipe (pfds) == 0,
        "buffer: hey I can has a pipe!");

    w = fbuf_write_watcher_create (reactor,
                                   pfds[1],
                                   1024,
                                   buffer_write,
                                   0,
                                   &count);
    ok (w == NULL && errno == EINVAL,
        "buffer: write_watcher_create fails with EINVAL if fd !nonblocking");

    ok (fd_set_nonblocking (pfds[1]) >= 0,
        "buffer: fd_set_nonblocking");

    w = fbuf_write_watcher_create (reactor,
                                   pfds[1],
                                   1024,
                                   buffer_write,
                                   0,
                                   &count);
    ok (w != NULL,
        "buffer: write watcher close: watcher created");
    fb = fbuf_write_watcher_get_buffer (w);
    ok (fb != NULL,
        "buffer: write watcher close: buffer retrieved");

    ok (fbuf_write (fb, "foobaz", 6) == 6,
        "buffer: write to buffer success");

    ok (fbuf_write_watcher_is_closed (w, NULL) == 0,
        "buffer: fbuf_write_watcher_is_closed returns false");
    ok (fbuf_write_watcher_close (w) == 0,
        "buffer: fbuf_write_watcher_close: Success");
    ok (fbuf_write_watcher_is_closed (w, NULL) == 0,
        "buffer: watcher still not closed (close(2) not called yet)");
    ok (fbuf_write_watcher_close (w) == -1 && errno == EINPROGRESS,
        "buffer: fbuf_write_watcher_close: In progress");

    ok (fbuf_write (fb, "shouldfail", 10) == -1 && errno == EROFS,
        "buffer: fbuf_write after close fails with EROFS");

    flux_watcher_start (w);

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer: reactor ran to completion");

    ok (count == 3,
        "buffer: write callback called 3 times");
    ok (fbuf_write_watcher_is_closed (w, &errnum) == 1 && errnum == 0,
        "buffer: fbuf_write_watcher_is_closed returns true");
    ok (fbuf_write_watcher_close (w) == -1 && errno == EINVAL,
        "buffer: fbuf_write_watcher_close after close returns EINVAL");

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
    fbuf_read_watcher_decref (bfc->w);
    ok (true, "fbuf_read_watcher_decref");
    flux_watcher_destroy (w);
}

static void buffer_read_fd_decref (flux_reactor_t *r,
                                   flux_watcher_t *w,
                                   int revents,
                                   void *arg)
{
    struct buffer_fd_close *bfc = arg;
    struct fbuf *fb;
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

    fb = fbuf_read_watcher_get_buffer (w);
    ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
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
        ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
                "buffer corner case: read from buffer success");

            ok (len == 6,
                "buffer corner case: read returned correct length");

            ok (!memcmp (ptr, "foobar", 6),
                "buffer corner case: read returned correct data");

            close (bfc->fd);
        }
        else {
            ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 7,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foobar\n", 7),
                "buffer corner case: read line returned correct data");

            close (bfc->fd);
        }
        else {
            ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
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
        struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
        const void *ptr;
        int len;

        if (!bfc->count) {
            ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 7,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foobar\n", 7),
                "buffer corner case: read line returned correct data");

            close (bfc->fd);
        }
        else if (bfc->count == 1) {
            ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
                "buffer corner case: read line from buffer success");

            ok (len == 0,
                "buffer corner case: read line says no lines available");

            ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
                "buffer corner case: read from buffer success");

            ok (len == 3,
                "buffer corner case: read line returned correct length");

            ok (!memcmp (ptr, "foo", 3),
                "buffer corner case: read line returned correct data");
        }
        else {
            ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
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

    ok (create_socketpair_nonblock (fd) == 0,
        "buffer decref: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = fbuf_read_watcher_create (reactor,
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

    diag ("calling fbuf_read_watcher_incref");
    fbuf_read_watcher_incref (w);

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
    struct fbuf *fb;
    struct buffer_fd_close bfc;

    /* read buffer corner case test - other end closes stream */

    ok (create_socketpair_nonblock (fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_fd_close,
                                  0,
                                  &bfc);
    ok (w != NULL,
        "buffer corner case: read created");

    ok (flux_watcher_is_active (w) == false,
        "flux_watcher_is_active() returns false on read buffer after create");

    fb = fbuf_read_watcher_get_buffer (w);

    ok (fb != NULL,
        "buffer corner case: buffer retrieved");

    ok (write (fd[1], "foobar", 6) == 6,
        "buffer corner case: write to socketpair success");

    flux_watcher_start (w);
    ok (flux_watcher_is_active (w) == true,
        "flux_watcher_is_active() returns true on read buffer after start");

    ok (flux_reactor_run (reactor, 0) == 0,
        "buffer corner case: reactor ran to completion");

    ok (bfc.count == 2,
        "buffer corner case: read callback successfully called twice");

    flux_watcher_stop (w);
    flux_watcher_destroy (w);

    close (fd[0]);

    /* read line buffer corner case test - other end closes stream */

    ok (create_socketpair_nonblock (fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_line_fd_close,
                                  FBUF_WATCHER_LINE_BUFFER,
                                  &bfc);
    ok (w != NULL,
        "buffer corner case: read line created");

    fb = fbuf_read_watcher_get_buffer (w);

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

    ok (create_socketpair_nonblock (fd) == 0,
        "buffer corner case: successfully created socketpair");

    bfc.count = 0;
    bfc.fd = fd[1];
    w = fbuf_read_watcher_create (reactor,
                                  fd[0],
                                  1024,
                                  buffer_read_line_fd_close_and_left_over_data,
                                  FBUF_WATCHER_LINE_BUFFER,
                                  &bfc);
    ok (w != NULL,
        "buffer corner case: read line created");

    fb = fbuf_read_watcher_get_buffer (w);

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

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_buffer (reactor);
    test_buffer_refcnt (reactor);
    test_buffer_corner_case (reactor);

    flux_reactor_destroy (reactor);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

