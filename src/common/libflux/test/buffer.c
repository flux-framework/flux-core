/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/libflux/buffer.h"
#include "src/common/libflux/buffer_private.h"
#include "src/common/libtap/tap.h"

#define FLUX_BUFFER_TEST_MAXSIZE 1048576

void empty_cb (flux_buffer_t *fb, void *arg)
{
    /* do nothing */
}

void basic (void)
{
    flux_buffer_t *fb;
    int pipefds[2];
    char buf[1024];
    const char *ptr;
    int len;

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_size (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_size returns correct size");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes initially returns 0");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    /* write & peek tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 3),
        "flux_buffer_space returns length of space left");

    ok ((ptr = flux_buffer_peek (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_peek with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_peek returns expected data");

    ok ((ptr = flux_buffer_peek (fb, -1, &len)) != NULL
        && len == 3,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "foo", 3),
        "flux_buffer_peek returns expected data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns unchanged length after peek");

    ok (flux_buffer_drop (fb, 2) == 2,
        "flux_buffer_drop works");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns length of remaining bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 1),
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_drop (fb, -1) == 1,
        "flux_buffer_drop drops remaining bytes");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes dropped");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    /* write and read tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 3),
        "flux_buffer_space returns length of space left");

    ok ((ptr = flux_buffer_read (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_read returns expected data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns new length after read");

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 1,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "flux_buffer_peek returns expected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes read");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    /* write_line & peek_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 4),
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");
    ok (flux_buffer_has_line (fb) == 1,
        "flux_buffer_has_line returns true on line written");

    ok ((ptr = flux_buffer_peek_line (fb, &len)) != NULL
        && len == 4,
        "flux_buffer_peek_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "flux_buffer_peek_line returns expected data");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns unchanged length after peek_line");

    ok (flux_buffer_drop_line (fb) == 4,
        "flux_buffer_drop_line works");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after drop_line");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after drop_line");

    /* write_line & peek_trimmed_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 4),
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");

    ok ((ptr = flux_buffer_peek_trimmed_line (fb, &len)) != NULL
        && len == 3,
        "flux_buffer_peek_trimmed_line works");

    ok (!memcmp (ptr, "foo", 3),
        "flux_buffer_peek_trimmed_line returns expected data");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns unchanged length after peek_trimmed_line");

    ok (flux_buffer_drop_line (fb) == 4,
        "flux_buffer_drop_line works");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after drop_line");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after drop_line");

    /* write_line & read_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 4),
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");

    ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL
        && len == 4,
        "flux_buffer_read_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "flux_buffer_read_line returns expected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after read_line");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after read_line");

    /* write_line & read_trimmed_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == (FLUX_BUFFER_TEST_MAXSIZE - 4),
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");

    ok ((ptr = flux_buffer_read_trimmed_line (fb, &len)) != NULL
        && len == 3,
        "flux_buffer_read_trimmed_line works");

    ok (!memcmp (ptr, "foo", 3),
        "flux_buffer_read_trimmed_line returns expected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after read_trimmed_line");

    ok (flux_buffer_space (fb) == FLUX_BUFFER_TEST_MAXSIZE,
        "flux_buffer_space initially returns FLUX_BUFFER_TEST_MAXSIZE");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after read_trimmed_line");

    /* peek_to_fd tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_peek_to_fd (fb, pipefds[1], 2) == 2,
        "flux_buffer_peek_to_fd specific length works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 2,
        "read correct number of bytes");

    ok (memcmp (buf, "fo", 2) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns correct length after peek");

    ok (flux_buffer_peek_to_fd (fb, pipefds[1], -1) == 3,
        "flux_buffer_peek_to_fd length -1 works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 3,
        "read correct number of bytes");

    ok (memcmp (buf, "foo", 3) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns correct length after peek");

    ok (flux_buffer_drop (fb, -1) == 3,
        "flux_buffer_drop drops remaining bytes");

    /* read_to_fd tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], 2) == 2,
        "flux_buffer_read_to_fd specific length works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 2,
        "read correct number of bytes");

    ok (memcmp (buf, "fo", 2) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns correct length after read");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], -1) == 1,
        "flux_buffer_read_to_fd length -1 works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 1,
        "read correct number of bytes");

    ok (memcmp (buf, "o", 1) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns correct length after read");

    /* write_from_fd and read tests */

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], -1) == 3,
        "flux_buffer_write_from_fd works");

    ok ((ptr = flux_buffer_read (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_read returns expected data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns new length after read");

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 1,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "flux_buffer_peek returns expected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes read");

    flux_buffer_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

void read_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 6,
        "flux_buffer_read in callback works");

    ok (!memcmp (ptr, "foobar", 6),
        "read in callback returns expected data");
}

void read_line_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL
        && len == 7,
        "flux_buffer_read_line in callback works");

    ok (!memcmp (ptr, "foobar\n", 7),
        "read_line in callback returns expected data");
}

void write_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;

    (*count)++;

    ok (flux_buffer_write (fb, "a", 1) == 1,
        "flux_buffer_write in callback works");
}

void basic_callback (void)
{
    flux_buffer_t *fb;
    const char *ptr;
    int len;
    int pipefds[2];
    int count;
    char buf[1024];

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    /* low read callback w/ write */

    count = 0;
    ok (flux_buffer_set_low_read_cb (fb, read_cb, 3, &count) == 0,
        "flux_buffer_set_low_read_cb success");

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    ok (count == 1,
        "read_cb called");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 because callback read all data");

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write success");

    ok (count == 1,
        "read_cb not called again, because not above low mark");

    count = 0;
    ok (flux_buffer_set_low_read_cb (fb, NULL, 0, &count) == 0,
        "flux_buffer_set_low_read_cb clear callback success");

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write success");

    ok (count == 0,
        "read_cb cleared successfully");

    ok (flux_buffer_drop (fb, -1) == 6,
        "flux_buffer_drop cleared all data");

    /* read line callback w/ write_line */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    count = 0;
    ok (flux_buffer_set_read_line_cb (fb, read_line_cb, &count) == 0,
        "flux_buffer_set_read_line_cb success");

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write success");

    ok (count == 0,
        "read_line_cb not called, no line written yet");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write (fb, "bar\n", 4) == 4,
        "flux_buffer_write success");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 because callback read all data");

    ok (count == 1,
        "read_line_cb called");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line, callback read all data");

    count = 0;
    ok (flux_buffer_set_read_line_cb (fb, NULL, &count) == 0,
        "flux_buffer_set_read_line_cb clear callback success");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line success");

    ok (count == 0,
        "read_line_cb cleared successfully");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1, callback did not read line");

    ok (flux_buffer_drop (fb, -1) == 4,
        "flux_buffer_drop cleared all data");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after drop line");

    /* low read callback w/ write_from_fd */

    count = 0;
    ok (flux_buffer_set_low_read_cb (fb, read_cb, 3, &count) == 0,
        "flux_buffer_set_low_read_cb success");

    ok (write (pipefds[1], "foobar", 6) == 6,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], 6) == 6,
        "flux_buffer_write_from_fd success");

    ok (count == 1,
        "read_cb called");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 because callback read all data");

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], 3) == 3,
        "flux_buffer_write_from_fd success");

    ok (count == 1,
        "read_cb not called again, because not above low mark");

    count = 0;
    ok (flux_buffer_set_low_read_cb (fb, NULL, 0, &count) == 0,
        "flux_buffer_set_low_read_cb clear callback success");

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], 3) == 3,
        "flux_buffer_write_from_fd success");

    ok (count == 0,
        "read_cb cleared successfully");

    ok (flux_buffer_drop (fb, -1) == 6,
        "flux_buffer_drop cleared all data");

    /* high write callback w/ read */

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, write_cb, 3, &count) == 0,
        "flux_buffer_set_high_write_cb success");

    ok ((ptr = flux_buffer_read (fb, 3, &len)) != NULL
        && len == 3,
        "flux_buffer_read success");

    ok (!memcmp (ptr, "foo", 3),
        "flux_buffer_read returns expected data");

    ok (count == 0,
        "write_cb not called, not less than high");

    ok ((ptr = flux_buffer_read (fb, 3, &len)) != NULL
        && len == 3,
        "flux_buffer_read success");

    ok (!memcmp (ptr, "bar", 3),
        "flux_buffer_read returns expected data");

    ok (count == 1,
        "write_cb called");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns 1 because callback wrote a byte");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, NULL, 0, &count) == 0,
        "flux_buffer_set_high_write_cb clear callback success");

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 1,
        "flux_buffer_read success");

    ok (!memcmp (ptr, "a", 1),
        "flux_buffer_read returns expected data");

    ok (count == 0,
        "write_cb cleared successfully");

    /* high write callback w/ drop */

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, write_cb, 3, &count) == 0,
        "flux_buffer_set_high_write_cb success");

    ok (flux_buffer_drop (fb, 3) == 3,
        "flux_buffer_drop success");

    ok (count == 0,
        "write_cb not called, not less than high");

    ok (flux_buffer_drop (fb, 1) == 1,
        "flux_buffer_drop success");

    ok (count == 1,
        "write_cb called");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes return correct bytes after drop and write cb");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, NULL, 0, &count) == 0,
        "flux_buffer_set_high_write_cb clear callback success");

    ok (flux_buffer_drop (fb, 1) == 1,
        "flux_buffer_drop success");

    ok (count == 0,
        "write_cb cleared successfully");

    ok (flux_buffer_drop (fb, -1) == 2,
        "flux_buffer_drop success");

    /* high write callback w/ read_to_fd */

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, write_cb, 3, &count) == 0,
        "flux_buffer_set_high_write_cb success");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], 3) == 3,
        "flux_buffer_read_to_fd success");

    ok (count == 0,
        "write_cb not called, not less than high");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], 1) == 1,
        "flux_buffer_read_to_fd success");

    ok (count == 1,
        "write_cb called");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes return correct bytes after read_to_fd and write cb");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, NULL, 0, &count) == 0,
        "flux_buffer_set_high_write_cb clear callback success");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], 1) == 1,
        "flux_buffer_read_to_fd success");

    ok (count == 0,
        "write_cb cleared successfully");

    ok (flux_buffer_drop (fb, -1) == 2,
        "flux_buffer_drop success");

    /* drain pipe, place in if statement to avoid uncheck return warnings */
    if (read (pipefds[0], buf, 1024) < 0)
        fprintf (stderr, "read error: %s\n", strerror (errno));

    flux_buffer_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

void disable_read_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = flux_buffer_read (fb, 3, &len)) != NULL
        && len == 3,
        "flux_buffer_read in callback works");

    ok (!flux_buffer_set_low_read_cb (fb,
                                      NULL,
                                      0,
                                      NULL),
        "read cb successfully disabled");
}

void disable_read_line_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL
        && len == 4,
        "flux_buffer_read_line in callback works");

    ok (!flux_buffer_set_read_line_cb (fb,
                                       NULL,
                                       NULL),
        "read line cb successfully disabled");
}

void disable_write_cb (flux_buffer_t *fb, void *arg)
{
    int *count = arg;

    (*count)++;

    ok (!flux_buffer_set_high_write_cb (fb,
                                        NULL,
                                        0,
                                        NULL),
        "write cb successfully disabled");
}

void disable_callback (void)
{
    flux_buffer_t *fb;
    const char *ptr;
    int len;
    int count;

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    /* low read callback w/ write */

    count = 0;
    ok (flux_buffer_set_low_read_cb (fb, disable_read_cb, 0, &count) == 0,
        "flux_buffer_set_low_read_cb success");

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    ok (count == 1,
        "disable_read_cb called only once, disabling callback in callback worked");

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write success");

    ok (count == 1,
        "disable_read_cb not called again, callback is disabled");

    ok (flux_buffer_drop (fb, -1) == 6,
        "flux_buffer_drop cleared all data");

    /* read line callback w/ write_line */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");
    ok (!flux_buffer_has_line (fb),
        "flux_buffer_has_line returns false on no line");

    count = 0;
    ok (flux_buffer_set_read_line_cb (fb, disable_read_line_cb, &count) == 0,
        "flux_buffer_set_read_line_cb success");

    ok (flux_buffer_write (fb, "foo\nfoo\n", 8) == 8,
        "flux_buffer_write success");

    ok (count == 1,
        "disable_read_line_cb called only once, disabling callback in callback worked");

    ok (flux_buffer_write (fb, "foo\n", 4) == 4,
        "flux_buffer_write success");

    ok (count == 1,
        "disable_read_line_cb not called again, callback is disabled");

    ok (flux_buffer_drop (fb, -1) == 8,
        "flux_buffer_drop cleared all data");

    /* high write callback w/ read */

    ok (flux_buffer_write (fb, "foofoo", 6) == 6,
        "flux_buffer_write success");

    count = 0;
    ok (flux_buffer_set_high_write_cb (fb, disable_write_cb, 6, &count) == 0,
        "flux_buffer_set_high_write_cb success");

    ok ((ptr = flux_buffer_read (fb, 3, &len)) != NULL
        && len == 3,
        "flux_buffer_read success");

    ok (count == 1,
        "disable_write_cb called correct number of times");

    ok ((ptr = flux_buffer_read (fb, 3, &len)) != NULL
        && len == 3,
        "flux_buffer_read success");

    ok (count == 1,
        "disable_write_cb not called again, successfully disabled");

    flux_buffer_destroy (fb);
}

void corner_case (void)
{
    flux_buffer_t *fb;
    const char *ptr;
    int len;

    ok (flux_buffer_create (-1) == NULL
        && errno == EINVAL,
        "flux_buffer_create fails on bad input -1");

    ok (flux_buffer_create (0) == NULL
        && errno == EINVAL,
        "flux_buffer_create fails on bad input 0");

    /* all functions fail on NULL fb pointer */
    ok (flux_buffer_size (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_size fails on NULL pointer");
    ok (flux_buffer_bytes (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_bytes fails on NULL pointer");
    ok (flux_buffer_space (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_space fails on NULL pointer");
    ok (flux_buffer_readonly (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_readonly fails on NULL pointer");
    ok (flux_buffer_is_readonly (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_is_readonly fails on NULL pointer");
    ok (flux_buffer_set_low_read_cb (NULL, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_low_read_cb fails on NULL pointer");
    ok (flux_buffer_set_read_line_cb (NULL, empty_cb, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_read_line_cb fails on NULL pointer");
    ok (flux_buffer_set_high_write_cb (NULL, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_high_write_cb fails on NULL pointer");
    ok (flux_buffer_drop (NULL, -1) < 0
        && errno == EINVAL,
        "flux_buffer_drop fails on NULL pointer");
    ok (flux_buffer_peek (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek fails on NULL pointer");
    ok (flux_buffer_read (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read fails on NULL pointer");
    ok (flux_buffer_write (NULL, NULL, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on NULL pointer");
    ok (flux_buffer_lines (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_lines fails on NULL pointer");
    errno = 0;
    ok (!flux_buffer_has_line (NULL)
        && errno == EINVAL,
        "flux_buffer_has_line returns false with errno set on NULL pointer");
    ok (flux_buffer_drop_line (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_drop_line fails on NULL pointer");
    ok (flux_buffer_peek_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek_line fails on NULL pointer");
    ok (flux_buffer_peek_trimmed_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek_trimmed_line fails on NULL pointer");
    ok (flux_buffer_read_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read_line fails on NULL pointer");
    ok (flux_buffer_read_trimmed_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read_trimmed_line fails on NULL pointer");
    ok (flux_buffer_write_line (NULL, "foo") < 0
        && errno == EINVAL,
        "flux_buffer_write_line fails on NULL pointer");
    ok (flux_buffer_peek_to_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_peek_to_fd fails on NULL pointer");
    ok (flux_buffer_read_to_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_read_to_fd fails on NULL pointer");
    ok (flux_buffer_write_from_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write_from_fd fails on NULL pointer");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    ok ((ptr = flux_buffer_peek (fb, -1, &len)) != NULL,
        "flux_buffer_peek works when no data available");
    ok (len == 0,
        "flux_buffer_peek returns length 0 when no data available");
    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL,
        "flux_buffer_read works when no data available");
    ok (len == 0,
        "flux_buffer_read returns length 0 when no data available");

    ok ((ptr = flux_buffer_peek_line (fb, &len)) != NULL,
        "flux_buffer_peek_line works when no data available");
    ok (len == 0,
        "flux_buffer_peek_line returns length 0 when no data available");
    ok ((ptr = flux_buffer_peek_trimmed_line (fb, &len)) != NULL,
        "flux_buffer_peek_trimmed_line works when no data available");
    ok (len == 0,
        "flux_buffer_peek_trimmed_line returns length 0 when no data available");
    ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL,
        "flux_buffer_read_line works when no data available");
    ok (len == 0,
        "flux_buffer_read_line returns length 0 when no data available");
    ok ((ptr = flux_buffer_read_trimmed_line (fb, &len)) != NULL,
        "flux_buffer_read_trimmed_line works when no data available");
    ok (len == 0,
        "flux_buffer_read_trimmed_line returns length 0 when no data available");

    /* callback corner case tests */

    ok (flux_buffer_set_low_read_cb (fb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_low_read_cb fails on bad input");
    ok (flux_buffer_set_low_read_cb (fb, empty_cb, 0, NULL) == 0,
        "flux_buffer_set_low_read_cb success");
    ok (flux_buffer_set_low_read_cb (fb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_low_read_cb fails on bad input overwrite callback");
    ok (flux_buffer_set_read_line_cb (fb, empty_cb, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_read_line_cb fails if callback already set");
    ok (flux_buffer_set_high_write_cb (fb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_high_write_cb fails if callback already set");
    ok (flux_buffer_set_low_read_cb (fb, NULL, 0, NULL) == 0,
        "flux_buffer_set_low_read_cb success clear callback");

    ok (flux_buffer_set_read_line_cb (fb, empty_cb, NULL) == 0,
        "flux_buffer_set_read_line_cb success");
    ok (flux_buffer_set_low_read_cb (fb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_low_read_cb fails if callback already set");
    ok (flux_buffer_set_high_write_cb (fb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_high_write_cb fails if callback already set");
    ok (flux_buffer_set_read_line_cb (fb, NULL, NULL) == 0,
        "flux_buffer_set_read_line_cb success clear callback");

    ok (flux_buffer_set_high_write_cb (fb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_high_write_cb fails on bad input");
    ok (flux_buffer_set_high_write_cb (fb, empty_cb, 0, NULL) == 0,
        "flux_buffer_set_high_write_cb success");
    ok (flux_buffer_set_high_write_cb (fb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_set_high_write_cb fails on bad input overwrite callback");
    ok (flux_buffer_set_low_read_cb (fb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_low_read_cb fails if callback already set");
    ok (flux_buffer_set_read_line_cb (fb, empty_cb, NULL) < 0
        && errno == EEXIST,
        "flux_buffer_set_read_line_cb fails if callback already set");
    ok (flux_buffer_set_high_write_cb (fb, NULL, 0, NULL) == 0,
        "flux_buffer_set_high_write_cb success clear callback");

    /* write corner case tests */

    ok (flux_buffer_write (fb, NULL, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on bad input");
    ok (flux_buffer_write (fb, "foo", -1) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on bad input");
    ok (flux_buffer_write_line (fb, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_write_line fails on bad input");
    ok (flux_buffer_write_from_fd (fb, -1, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write_from_fd fails on bad input");

    /* flux_buffer_destroy works with NULL */
    flux_buffer_destroy (NULL);

    flux_buffer_destroy (fb);
}

void full_buffer (void)
{
    flux_buffer_t *fb;

    ok ((fb = flux_buffer_create (4)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_write (fb, "1234", 4) == 4,
        "flux_buffer_write success");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_space (fb) == 0,
        "flux_buffer_space returns length of space left");

    ok (flux_buffer_write (fb, "5", 1) < 0
        && errno == ENOSPC,
        "flux_buffer_write fails with ENOSPC if exceeding buffer size");

    ok (flux_buffer_drop (fb, -1) == 4,
        "flux_buffer_drop works");

    ok (flux_buffer_write_line (fb, "1234") < 0
        && errno == ENOSPC,
        "flux_buffer_write_line fails with ENOSPC if exceeding buffer size");

    flux_buffer_destroy (fb);
}

void readonly_buffer (void)
{
    flux_buffer_t *fb;
    int pipefds[2];

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_is_readonly (fb) == 0,
        "flux buffer is not readonly on creation");

    ok (flux_buffer_readonly (fb) == 0,
        "flux buffer readonly set");

    ok (flux_buffer_is_readonly (fb) > 0,
        "flux buffer is readonly after setting");

    flux_buffer_destroy (fb);

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_write (fb, "foobar", 6) == 6,
        "flux_buffer_write success");

    ok (flux_buffer_readonly (fb) == 0,
        "flux buffer readonly set");

    ok (flux_buffer_write (fb, "foobar", 6) < 0
        && errno == EROFS,
        "flux_buffer_write fails b/c readonly is set");

    ok (flux_buffer_write_line (fb, "foobar") < 0
        && errno == EROFS,
        "flux_buffer_write_line fails b/c readonly is set");

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], -1) < 0
        && errno == EROFS,
        "flux_buffer_write_from_fd fails b/c readonly is set");

    flux_buffer_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    basic_callback ();
    disable_callback ();
    corner_case ();
    full_buffer ();
    readonly_buffer ();

    done_testing();

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

