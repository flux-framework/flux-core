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

bool input_called;
bool input_eof_set;

void input_cb (flux_reactor_t *r,
               flux_watcher_t *w,
               int revents,
               void *arg)
{
    struct channel *ch = arg;
    const char *name = sdexec_channel_get_name (ch);
    int fd = sdexec_channel_get_fd (ch);
    char buf[64];
    int n;

    n = read (fd, buf, sizeof (buf));
    if (n < 0) {
        diag ("%s: read error: %s", name, strerror (errno));
    }
    else if (n == 0) {
        diag ("%s: EOF", name);
        input_eof_set = true;
    }
    else
        diag ("%s: read %d chars", name, n);
    input_called = true;
}

void test_input (void)
{
    flux_t *h;
    struct channel *ch;
    flux_watcher_t *w;
    int fd;
    json_t *io;
    json_t *io_eof;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank for testing");
    ch = sdexec_channel_create_input (h, "in");
    ok (ch != NULL,
        "sdexec_channel_create_input works");
    fd = sdexec_channel_get_fd (ch);
    ok (fd >= 0,
        "sdexec_channel_get_fd works");
    w = flux_fd_watcher_create (flux_get_reactor (h),
                                fd,
                                FLUX_POLLIN,
                                input_cb,
                                ch);
    if (!w)
        BAIL_OUT ("could not create fd watcher");
    if (!(io = ioencode ("foo", "0", "hello", 6, false)))
        BAIL_OUT ("could not create json io object");
    if (!(io_eof = ioencode ("foo", "0", NULL, 0, true)))
        BAIL_OUT ("could not create json io_eof object");
    flux_watcher_start (w);

    input_called = false;
    input_eof_set = false;
    ok (sdexec_channel_write (ch, io) == 0,
        "sdexec_channel_write works");
    ok (flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ran ONCE");
    ok (input_called == true,
        "input callback was called");
    ok (input_eof_set == false,
        "eof was not set");

    input_called = false;
    input_eof_set = false;
    ok (sdexec_channel_write (ch, io_eof) == 0,
        "sdexec_channel_write eof works");
    ok (flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ran ONCE");
    ok (input_called == true,
        "input callback was called");
    ok (input_eof_set == true,
        "eof was set");

    json_decref (io_eof);
    json_decref (io);
    flux_watcher_destroy (w);
    sdexec_channel_destroy (ch);
    flux_close (h);
}

bool error_called;
bool output_called;
bool output_eof_set;

void output_cb (struct channel *ch, json_t *io, void *arg)
{
    const char *stream;
    int len;

    if (iodecode (io, &stream, NULL, NULL, &len, &output_eof_set) < 0) {
        diag ("%s: idoecode error: %s",
              sdexec_channel_get_name (ch),
              strerror (errno));
    }
    else {
        diag ("%s output: stream=%s len=%d eof=%s",
              sdexec_channel_get_name (ch),
              stream,
              len,
              output_eof_set ? "true" : "false");
    }
    output_called = true;
}

void error_cb (struct channel *ch, flux_error_t *error, void *arg)
{
    diag ("%s error: %s",
          sdexec_channel_get_name (ch),
          error->text);
    error_called = true;
}

void test_output (void)
{
    flux_t *h;
    struct channel *ch;
    int fd;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank for testing");
    ch = sdexec_channel_create_output (h, "out", 0, 0, output_cb, error_cb, NULL);
    ok (ch != NULL,
        "sdexec_channel_crate_output works");
    sdexec_channel_start_output (ch);
    ok (true, "sdexec_channel_start_output called");

    fd = sdexec_channel_get_fd (ch);
    ok (fd >= 0,
        "sdexec_channel_get_fd works");

    output_called = false;
    error_called = false;
    output_eof_set = false;
    ok (write (fd, "hello", 6) == 6,
        "wrote 'hello' from unit");
    ok (flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ran ONCE");
    ok (output_called == true,
        "output callback was called");
    ok (output_eof_set == false,
        "eof was not set");
    ok (error_called == false,
        "error callback was not called");

    output_called = false;
    error_called = false;
    output_eof_set = false;
    sdexec_channel_close_fd (ch);
    ok (true, "sdexec_channel_close_fd called");
    ok (flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ran ONCE");
    ok (output_called == true,
        "output callback was called");
    ok (output_eof_set == true,
        "eof was set");
    ok (error_called == false,
        "error callback was not called");

    sdexec_channel_destroy (ch);
    flux_close (h);
}

void test_inval (void)
{
    struct channel *ch;
    flux_t *h;
    json_t *io;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop flux_t handle for testing");
    if (!(io = ioencode ("foo", "0", NULL, 0, true)))
        BAIL_OUT ("could not create json io object");

    errno = 0;
    ch = sdexec_channel_create_output (NULL, "foo", 0, 0, output_cb, error_cb, NULL);
    ok (ch == NULL && errno == EINVAL,
        "sdexec_channel_create_output h=NULL fails with EINVAL");
    errno = 0;
    ch = sdexec_channel_create_output (h, NULL, 0, 0, output_cb, error_cb, NULL);
    ok (ch == NULL && errno == EINVAL,
        "sdexec_channel_create_output name=NULL fails with EINVAL");

    errno = 0;
    ch = sdexec_channel_create_input (NULL, "foo");
    ok (ch == NULL && errno == EINVAL,
        "sdexec_channel_create_input h=NULL fails with EINVAL");
    errno = 0;
    ch = sdexec_channel_create_input (h, NULL);
    ok (ch == NULL && errno == EINVAL,
        "sdexec_channel_create_input name=NULL fails with EINVAL");

    errno = 0;
    ok (sdexec_channel_write (NULL, io) < 0 && errno == EINVAL,
        "sdexec_channel_write ch=NULL fails with EINVAL");

    ok (sdexec_channel_get_fd (NULL) == -1,
        "sdexec_channel_get_fd ch=NULL returns -1");

    ok (sdexec_channel_get_name (NULL) != NULL,
        "sdexec_channel_get_name ch=NULL returns non-NULL");

    lives_ok ({sdexec_channel_start_output (NULL);},
              "sdexec_channel_start_output ch=NULL doesn't crash");
    lives_ok ({sdexec_channel_close_fd (NULL);},
              "sdexec_channel_close_fd ch=NULL doesn't crash");
    lives_ok ({sdexec_channel_destroy (NULL);},
              "sdexec_channel_destroy ch=NULL doesn't crash");

    json_decref (io);
    flux_close (h);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_input ();
    test_output ();
    test_inval ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
