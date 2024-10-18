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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/libtap/tap.h"

#include "fbuf.h"

#define FBUF_TEST_MAXSIZE 1048576

void basic (void)
{
    struct fbuf *fb;
    int pipefds[2];
    char buf[1024];
    const char *ptr;
    int len;

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = fbuf_create (FBUF_TEST_MAXSIZE)) != NULL,
        "fbuf_create works");

    ok (fbuf_size (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_size returns correct size");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes initially returns 0");

    ok (fbuf_space (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_space initially returns FBUF_TEST_MAXSIZE");

    /* write and read tests */

    ok (fbuf_write (fb, "foo", 3) == 3,
        "fbuf_write works");

    ok (fbuf_bytes (fb) == 3,
        "fbuf_bytes returns length of bytes written");

    ok (fbuf_space (fb) == (FBUF_TEST_MAXSIZE - 3),
        "fbuf_space returns length of space left");

    ok ((ptr = fbuf_read (fb, 2, &len)) != NULL
        && len == 2,
        "fbuf_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "fbuf_read returns expected data");

    ok (fbuf_bytes (fb) == 1,
        "fbuf_bytes returns new length after read");

    ok ((ptr = fbuf_read (fb, -1, &len)) != NULL
        && len == 1,
        "fbuf_read with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "fbuf_read returns expected data");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes returns 0 with all bytes read");

    ok (fbuf_space (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_space initially returns FBUF_TEST_MAXSIZE");

    /* read_line tests */

    ok (fbuf_write (fb, "foo\n", 4) == 4,
        "fbuf_write works");

    ok (fbuf_bytes (fb) == 4,
        "fbuf_bytes returns length of bytes written");

    ok (fbuf_space (fb) == (FBUF_TEST_MAXSIZE - 4),
        "fbuf_space returns length of space left");

    ok ((ptr = fbuf_read_line (fb, &len)) != NULL
        && len == 4,
        "fbuf_read_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "fbuf_read_line returns expected data");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes returns 0 after read_line");

    ok (fbuf_space (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_space initially returns FBUF_TEST_MAXSIZE");

    /* read_to_fd tests */

    ok (fbuf_write (fb, "foo", 3) == 3,
        "fbuf_write works");

    ok (fbuf_read_to_fd (fb, pipefds[1], 2) == 2,
        "fbuf_read_to_fd specific length works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 2,
        "read correct number of bytes");

    ok (memcmp (buf, "fo", 2) == 0,
        "read returned correct data");

    ok (fbuf_bytes (fb) == 1,
        "fbuf_bytes returns correct length after read");

    ok (fbuf_read_to_fd (fb, pipefds[1], -1) == 1,
        "fbuf_read_to_fd length -1 works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 1,
        "read correct number of bytes");

    ok (memcmp (buf, "o", 1) == 0,
        "read returned correct data");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes returns correct length after read");

    /* write_from_fd and read tests */

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (fbuf_write_from_fd (fb, pipefds[0], -1) == 3,
        "fbuf_write_from_fd works");

    ok ((ptr = fbuf_read (fb, 2, &len)) != NULL
        && len == 2,
        "fbuf_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "fbuf_read returns expected data");

    ok (fbuf_bytes (fb) == 1,
        "fbuf_bytes returns new length after read");

    ok ((ptr = fbuf_read (fb, -1, &len)) != NULL
        && len == 1,
        "fbuf_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "fbuf_peek returns expected data");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes returns 0 with all bytes read");

    fbuf_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

void notify_cb (struct fbuf *fb, void *arg)
{
    int *count = arg;
    (*count)++;
}

void notify_callback (void)
{
    struct fbuf *fb;
    int count;
    int len;

    ok ((fb = fbuf_create (16)) != NULL,
        "fbuf_create 16 byte buffer works");
    fbuf_set_notify (fb, notify_cb, &count);

    count = 0;

    ok (fbuf_write (fb, "foobar", 6) == 6,
        "fbuf_write 6 bytes");

    ok (count == 1,
        "notify was called on transition from empty");

    ok (fbuf_write (fb, "foo", 3) == 3,
        "fbuf_write 3 bytes");

    ok (count == 2,
        "notify was called again");

    ok (fbuf_write (fb, "1234567", 7) == 7,
        "fbuf_write 7 bytes success");

    ok (count == 3,
        "notify was called again on transition to full");

    ok (fbuf_read (fb, 1, &len) != NULL && len == 1,
        "fbuf_read cleared one byte");

    ok (count == 4,
        "notify was called again on transition from full");

    ok (fbuf_read (fb, -1, &len) != NULL && len == 15,
        "fbuf_read cleared all data");

    ok (count == 5,
        "notify was called on transition to empty");

    fbuf_destroy (fb);
}

void corner_case (void)
{
    struct fbuf *fb;
    const char *ptr;
    int len;

    ok (fbuf_create (-1) == NULL
        && errno == EINVAL,
        "fbuf_create fails on bad input -1");

    ok (fbuf_create (0) == NULL
        && errno == EINVAL,
        "fbuf_create fails on bad input 0");

    /* all functions fail on NULL fb pointer */
    ok (fbuf_size (NULL) < 0
        && errno == EINVAL,
        "fbuf_size fails on NULL pointer");
    ok (fbuf_bytes (NULL) < 0
        && errno == EINVAL,
        "fbuf_bytes fails on NULL pointer");
    ok (fbuf_space (NULL) < 0
        && errno == EINVAL,
        "fbuf_space fails on NULL pointer");
    ok (fbuf_readonly (NULL) < 0
        && errno == EINVAL,
        "fbuf_readonly fails on NULL pointer");
    errno = 0;
    ok (!fbuf_is_readonly (NULL) && errno == EINVAL,
        "fbuf_is_readonly returns false on NULL pointer");
    ok (fbuf_read (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "fbuf_read fails on NULL pointer");
    ok (fbuf_write (NULL, NULL, 0) < 0
        && errno == EINVAL,
        "fbuf_write fails on NULL pointer");
    errno = 0;
    ok (!fbuf_has_line (NULL)
        && errno == EINVAL,
        "fbuf_has_line returns false with errno set on NULL pointer");
    ok (fbuf_read_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "fbuf_read_line fails on NULL pointer");
    ok (fbuf_read_trimmed_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "fbuf_read_trimmed_line fails on NULL pointer");
    ok (fbuf_read_to_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "fbuf_read_to_fd fails on NULL pointer");
    ok (fbuf_write_from_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "fbuf_write_from_fd fails on NULL pointer");

    ok ((fb = fbuf_create (FBUF_TEST_MAXSIZE)) != NULL,
        "fbuf_create works");

    ok ((ptr = fbuf_read (fb, -1, &len)) != NULL,
        "fbuf_read works when no data available");
    ok (len == 0,
        "fbuf_read returns length 0 when no data available");

    ok ((ptr = fbuf_read_line (fb, &len)) != NULL,
        "fbuf_read_line works when no data available");
    ok (len == 0,
        "fbuf_read_line returns length 0 when no data available");
    ok ((ptr = fbuf_read_trimmed_line (fb, &len)) != NULL,
        "fbuf_read_trimmed_line works when no data available");
    ok (len == 0,
        "fbuf_read_trimmed_line returns length 0 when no data available");

    /* write corner case tests */

    ok (fbuf_write (fb, NULL, 0) < 0
        && errno == EINVAL,
        "fbuf_write fails on bad input");
    ok (fbuf_write (fb, "foo", -1) < 0
        && errno == EINVAL,
        "fbuf_write fails on bad input");
    ok (fbuf_write_from_fd (fb, -1, 0) < 0
        && errno == EINVAL,
        "fbuf_write_from_fd fails on bad input");

    /* fbuf_destroy works with NULL */
    fbuf_destroy (NULL);

    fbuf_destroy (fb);
}

void full_buffer (void)
{
    struct fbuf *fb;
    int len;

    ok ((fb = fbuf_create (4)) != NULL,
        "fbuf_create works");

    ok (fbuf_write (fb, "1234", 4) == 4,
        "fbuf_write success");

    ok (fbuf_bytes (fb) == 4,
        "fbuf_bytes returns length of bytes written");

    ok (fbuf_space (fb) == 0,
        "fbuf_space returns length of space left");

    ok (fbuf_write (fb, "5", 1) < 0
        && errno == ENOSPC,
        "fbuf_write fails with ENOSPC if exceeding buffer size");

    ok (fbuf_read (fb, -1, &len) != NULL && len == 4,
        "fbuf_read drops all data");

    fbuf_destroy (fb);
}

void readonly_buffer (void)
{
    struct fbuf *fb;
    int pipefds[2];

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = fbuf_create (FBUF_TEST_MAXSIZE)) != NULL,
        "fbuf_create works");

    ok (!fbuf_is_readonly (fb),
        "flux buffer is not readonly on creation");

    ok (fbuf_readonly (fb) == 0,
        "flux buffer readonly set");

    ok (fbuf_is_readonly (fb),
        "flux buffer is readonly after setting");

    fbuf_destroy (fb);

    ok ((fb = fbuf_create (FBUF_TEST_MAXSIZE)) != NULL,
        "fbuf_create works");

    ok (fbuf_write (fb, "foobar", 6) == 6,
        "fbuf_write success");

    ok (fbuf_readonly (fb) == 0,
        "flux buffer readonly set");

    ok (fbuf_write (fb, "foobar", 6) < 0
        && errno == EROFS,
        "fbuf_write fails b/c readonly is set");

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (fbuf_write_from_fd (fb, pipefds[0], -1) < 0
        && errno == EROFS,
        "fbuf_write_from_fd fails b/c readonly is set");

    fbuf_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

/* tests to ensure internal buffers grow appropriately.  Current
 * buffer default min is 4096, so we need to test > 4096 bytes of
 * data.
 */
void large_data (void)
{
    struct fbuf *fb;
    const char *data = "0123456789ABCDEF0123456789ABCDEF";
    const char *ptr;
    int len;
    int i;

    ok ((fb = fbuf_create (FBUF_TEST_MAXSIZE)) != NULL,
        "fbuf_create works");

    ok (fbuf_size (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_size returns correct size");

    ok (fbuf_bytes (fb) == 0,
        "fbuf_bytes initially returns 0");

    ok (fbuf_space (fb) == FBUF_TEST_MAXSIZE,
        "fbuf_space initially returns FBUF_TEST_MAXSIZE");

    for (i = 0; i < 256; i++) {
        if (fbuf_write (fb, data, 32) != 32)
            ok (false, "fbuf_write fail: %s", strerror (errno));
    }

    ok (fbuf_space (fb) == (FBUF_TEST_MAXSIZE - 8192),
        "fbuf_space returns length of space left");

    ok ((ptr = fbuf_read (fb, -1, &len)) != NULL
        && len == 8192,
        "fbuf_read with length -1 works");

    for (i = 0; i < 256; i++) {
        if (memcmp (ptr + (i * 32), data, 32) != 0)
            ok (false, "fbuf_read returned bad data");
    }

    fbuf_destroy (fb);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    notify_callback ();
    corner_case ();
    full_buffer ();
    readonly_buffer ();
    large_data ();

    done_testing();

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

