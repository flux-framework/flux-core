/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "ccan/array_size/array_size.h"
#include "src/common/libtap/tap.h"
#include "src/common/libioencode/ioencode.h"
#include "channel.h"

bool error_called;

void error_cb (struct channel *ch, flux_error_t *error, void *arg)
{
    diag ("%s error: %s",
          sdexec_channel_get_name (ch),
          error->text);
    error_called = true;
}

size_t raw_byte_count;

void raw_output_cb (struct channel *ch, json_t *io, void *arg)
{
    flux_reactor_t *r = arg;
    const char *stream;
    int len;
    bool eof;

    if (iodecode (io, &stream, NULL, NULL, &len, &eof) < 0) {
        diag ("%s: idoecode error: %s",
              sdexec_channel_get_name (ch),
              strerror (errno));
    }
    else {
        diag ("%s output: stream=%s len=%d eof=%s",
              sdexec_channel_get_name (ch),
              stream,
              len,
              eof ? "true" : "false");
        raw_byte_count += len;
        if (eof)
            flux_reactor_stop (r);
    }
}

void test_raw (size_t bufsize, size_t datasize)
{
    flux_t *h;
    struct channel *ch;
    int fd;
    char buf[datasize];
    flux_reactor_t *r;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    r = flux_get_reactor (h);
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank for testing");
    ch = sdexec_channel_create_output (h,
                                       "raw",
                                       bufsize,
                                       0,
                                       raw_output_cb,
                                       error_cb,
                                       r);
    ok (ch != NULL,
        "sdexec_channel_crate_output works");
    sdexec_channel_start_output (ch);
    ok (true, "sdexec_channel_start_output called");

    fd = sdexec_channel_get_fd (ch);
    ok (fd >= 0,
        "sdexec_channel_get_fd works");

    raw_byte_count = 0;
    error_called = false;
    // write can exceed TINY_OUTBUF size by approx O(PAGE_SIZE) socket buf
    memset (buf, 'x', sizeof (buf));
    ok (write (fd, buf, sizeof (buf)) == sizeof (buf),
        "wrote %zu bytes of data from unit", sizeof (buf));
    sdexec_channel_close_fd (ch);
    ok (true, "sdexec_channel_close_fd called");
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run ran successfully");
    ok (error_called == false,
        "error callback was not called");
    ok (raw_byte_count == sizeof (buf),
        "all bytes were received");

    sdexec_channel_destroy (ch);
    flux_close (h);
}

size_t line_byte_count;
size_t line_count;
size_t line_calls;

void line_output_cb (struct channel *ch, json_t *io, void *arg)
{
    flux_reactor_t *r = arg;
    const char *stream;
    char *data = NULL;
    int len;
    bool eof;

    if (iodecode (io, &stream, NULL, &data, &len, &eof) < 0) {
        diag ("%s: idoecode error: %s",
              sdexec_channel_get_name (ch),
              strerror (errno));
    }
    else {
        diag ("%s output: stream=%s len=%d eof=%s",
              sdexec_channel_get_name (ch),
              stream,
              len,
              eof ? "true" : "false");
        line_byte_count += len;
        int count = 0;
        for (int i = 0; i < len; i++) {
            if (data[i] == '\n')
                count++;
        }
        line_count += count;
        line_calls++;
        free (data);
        if (eof)
            flux_reactor_stop (r);
    }
}

void test_line (size_t bufsize, size_t linelength, size_t datasize)
{
    flux_t *h;
    struct channel *ch;
    int fd;
    char buf[linelength];
    flux_reactor_t *r;
    size_t dataused;

    diag ("line test with bufsize=%zu linelength=%zu datasize=%zu",
          bufsize,
          linelength,
          datasize);

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    r = flux_get_reactor (h);
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank for testing");
    ch = sdexec_channel_create_output (h,
                                       "line",
                                       bufsize,
                                       CHANNEL_LINEBUF,
                                       line_output_cb,
                                       error_cb,
                                       r);
    ok (ch != NULL,
        "sdexec_channel_crate_output works");
    sdexec_channel_start_output (ch);
    ok (true, "sdexec_channel_start_output called");

    fd = sdexec_channel_get_fd (ch);
    ok (fd >= 0,
        "sdexec_channel_get_fd works");

    line_byte_count = 0;
    line_count = 0;
    line_calls = 0;
    error_called = false;
    memset (buf, 'x', sizeof (buf) - 1);
    buf[sizeof (buf) - 1] = '\n';
    dataused = 0;
    while (dataused < datasize) {
        size_t len = sizeof (buf);
        if (len > datasize - dataused)
            len = datasize - dataused;
        // write can exceed TINY_OUTBUF size by approx O(PAGE_SIZE) socket buf
        ok (write (fd, buf, len) == len,
            "wrote %zu bytes of data from unit", len);
        dataused += len;
    }
    sdexec_channel_close_fd (ch);
    ok (true, "sdexec_channel_close_fd called");
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run ran successfully");
    ok (error_called == false,
        "error callback was not called");
    ok (line_byte_count == datasize,
        "all bytes were received");

    diag ("lines %zu calls %zu", line_count, line_calls);
    size_t expected_line_count = datasize / linelength;
    size_t expected_calls = expected_line_count;

    /* If the lines are larger than the buffer, then each full line will
     * be transmitted in 2 callback - first one buffer's worth, then the
     * terminated fragment.  This assumes linelength is at most bufsize*2.
     */
    if (bufsize < linelength)
        expected_calls *= 2;

    /* The final "line" isn't terminated if datasize is not a multiple of
     * the linelength. The callback will get that + eof in one go.  Otherwise,
     * the eof will come through on its own.  Either way, one extra call.
     */
    expected_calls += 1;

    ok (line_count == expected_line_count,
        "expected number of lines (%zu) were received", expected_line_count);
    ok (line_calls == expected_calls,
        "expected number of callbacks (%zu) were made", expected_calls);

    sdexec_channel_destroy (ch);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_raw (16, 47);
    test_raw (4096, 3000);
    test_raw (4096, 6000);

    test_line (16, 4, 64); // 16 lines that fit perfectly
    test_line (16, 4, 63); // 15 lines + last one truncated
    test_line (15, 16, 32); // 2 lines split into 4 callbacks (short buffer)

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
