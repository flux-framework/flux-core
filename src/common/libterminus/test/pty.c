/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <errno.h>

#include <flux/core.h>
#include "src/common/libterminus/pty.h"

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

static void test_invalid_args ()
{
    struct flux_pty *pty = flux_pty_open ();
    struct flux_pty_client *c = flux_pty_client_create ();

    if (pty == NULL || c == NULL)
        BAIL_OUT ("Failed to create pty client/server!");

    lives_ok ({flux_pty_set_log (NULL, NULL, NULL);},
              "flux_pty_set_log does nothing with NULL args");
    lives_ok ({flux_pty_destroy (NULL);},
              "flux_pty_destroy does nothing with NULL arg");

    ok (flux_pty_kill (NULL, SIGINT) < 0 && errno == EINVAL,
        "flux_pty_kill() with NULL pty returns EINVAL");
    ok (flux_pty_kill (pty, -1) < 0 && errno == EINVAL,
        "flux_pty_kill() with invalid signal returns EINVAL");
    ok (flux_pty_leader_fd (NULL) < 0 && errno == EINVAL,
        "flux_pty_leader_fd() returns EINVAL with NULL arg");
    ok (flux_pty_name (NULL) == NULL && errno == EINVAL,
        "flux_pty_name() returns EINVAL with NULL arg");
    ok (flux_pty_attach (NULL) < 0 && errno == EINVAL,
        "flux_pty_attach() returns EINVAL with NULL arg");
    ok (flux_pty_client_attached (NULL) == false,
        "flux_pty_client_attached() returns false with NULL arg");
    ok (flux_pty_set_flux (NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_set_flux() returns EINVAL with NULL args");
    ok (flux_pty_set_flux (pty, NULL) < 0 && errno == EINVAL,
        "flux_pty_set_flux() returns EINVAL with NULL flux handle");
    ok (flux_pty_client_count (NULL) == 0,
        "flux_pty_client_count returns 0 for NULL pty");
    ok (flux_pty_disconnect_client (NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_disconnect_client returns EINVAL on NULL args");
    ok (flux_pty_disconnect_client (pty, NULL) < 0 && errno == EINVAL,
        "flux_pty_disconnect_client returns EINVAL on NULL sender");

    lives_ok ({flux_pty_client_set_log (NULL, NULL, NULL);},
              "flux_pty_client_set_log does nothing with NULL args");
    lives_ok ({flux_pty_client_destroy (NULL);},
              "flux_pty_client_destroy (NULL) does nothing");

    ok (flux_pty_client_attach (c, NULL, 0, NULL) < 0
        && errno == EINVAL,
        "flux_pty_client_attach returns EINVAL with NULL args");
    ok (flux_pty_client_attach (NULL, NULL, 0, NULL) < 0
        && errno == EINVAL,
        "flux_pty_client_attach returns EINVAL with NULL handle");

    ok (flux_pty_client_notify_exit (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_client_notify_exit() returns EINVAL on NULL client");
    ok (flux_pty_client_notify_exit (c, NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_client_notify_exit() returns EINVAL on NULL args");

    ok (flux_pty_client_write (NULL, NULL, 0) == NULL && errno == EINVAL,
        "flux_pty_client_write() returns EINVAL on NULL args");

    ok (flux_pty_client_set_flags (NULL, 0) < 0 && errno == EINVAL,
        "flux_pty_client_set_flags returns EINVAL on NULL arg");
    ok (flux_pty_client_set_flags (c, -1) < 0 && errno == EINVAL,
        "flux_pty_client_set_flags returns EINVAL on bad flags");

    ok (flux_pty_client_get_flags (NULL) < 0 && errno == EINVAL,
        "flux_pty_client_gett_flags returns EINVAL on NULL arg");

    ok (flux_pty_client_exit_status (NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_client_get_exit_status returns EINVAL on NULL args");
    ok (flux_pty_client_exit_status (c, NULL) < 0 && errno == EINVAL,
        "flux_pty_client_get_exit_status returns EINVAL on NULL statusp");

    lives_ok ({flux_pty_monitor (NULL, NULL);},
             "flux_pty_monitor can safely be called with NULL");

    ok (flux_pty_aux_set (NULL, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_pty_aux_set (NULL) fails");
    ok (flux_pty_aux_get (NULL, NULL) == NULL && errno == EINVAL,
        "flux_pty_aux_get (NULL, NULL) fails");

    flux_pty_destroy (pty);
    flux_pty_client_destroy (c);
}

static void test_empty_server ()
{
    struct flux_pty *pty = flux_pty_open ();

    ok (pty != NULL,
        "flux_pty_open works");
    ok (flux_pty_leader_fd (pty) >= 0,
        "pty leader fd is valid");
    ok (flux_pty_client_count (pty) == 0,
        "pty client count is 0 for newly created pty server");
    flux_pty_destroy (pty);
}

static void tap_logger (void *arg,
                        const char *file,
                        int line,
                        const char *func,
                        const char *subsys,
                        int level,
                        const char *fmt,
                        va_list ap)
{
    struct flux_pty *pty = arg;
    char buf [4096];
    int len = sizeof (buf);
    if (vsnprintf (buf, len, fmt, ap) >= len) {
        buf[len-1] = '\0';
        buf[len-2] = '+';
    }
    diag ("pty: %s: %s:%d %s(): %s",
          flux_pty_name (pty), file, line, func, buf);
}

static void pty_server_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct flux_pty *pty = arg;
    if (flux_pty_sendmsg (pty, msg) < 0)
        fail ("flux_pty_sendmsg returned -1: %s", strerror (errno));
}

static int pty_server (flux_t *h, void *arg)
{
    int rc = -1;
    struct flux_pty * pty = NULL;
    flux_msg_handler_t *mh = NULL;
    struct flux_match match = FLUX_MATCH_REQUEST;

    pty = flux_pty_open ();
    if (!pty)
        goto out;

    flux_pty_set_log (pty, tap_logger, pty);
    flux_pty_set_flux (pty, h);
    diag ("pty_server: opened %s", flux_pty_name (pty));

    mh = flux_msg_handler_create (h, match, pty_server_cb, pty);
    if (!mh)
        goto out;
    flux_msg_handler_start (mh);

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    diag ("pty server exiting");
out:
    flux_msg_handler_destroy (mh);
    flux_pty_destroy (pty);
    return rc;
}

static void test_basic_protocol (void)
{
    flux_t *h = test_server_create (0, pty_server, NULL);
    flux_future_t *f = NULL;
    flux_future_t *f_attach = NULL;

    /* invalid message, no msg type: */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0, "{}")) != NULL,
        "request: empty payload");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);


    /* attach without terminal size: */
    ok ((f = flux_rpc_pack (h, "pty", 0, FLUX_RPC_STREAMING,
                            "{ s:s s:s }",
                            "type", "attach",
                            "mode", "rw")) != NULL,
        "request: type attach, no winsize");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);


    /* attach without mode: */
    ok ((f = flux_rpc_pack (h, "pty", 0, FLUX_RPC_STREAMING,
                            "{ s:s s:{s:i s:i} }",
                            "type", "attach",
                            "winsize",
                               "rows", 25,
                               "cols", 80)) != NULL,
        "request: type attach, no mode");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);


    /* attach: invalid mode: */
    ok ((f = flux_rpc_pack (h, "pty", 0, FLUX_RPC_STREAMING,
                            "{ s:s s:s s:{s:i s:i} }",
                            "type", "attach",
                            "mode", "x",
                            "winsize",
                               "rows", 25,
                               "cols", 80)) != NULL,
        "request: type attach, bad mode");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);

    /* write from unattached client: */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{ s:s s:s }",
                            "type", "data",
                            "data", "\r")) != NULL,
        "request: type data, unconnected client");
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOENT,
        "response: ENOENT");
    flux_future_destroy (f);

    /* resize from unattached client: */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s s:{s:i s:i}}",
                            "type", "resize",
                            "winsize",
                               "rows", 25,
                               "cols", 80)) != NULL,
        "request: type resize, unconnected client");
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOENT,
        "response: ENOENT");
    flux_future_destroy (f);


    /* detach from unattached client: */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s}", "type", "detach")) != NULL,
        "request: type detach, unconnected client");
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOENT,
        "response: ENOENT");
    flux_future_destroy (f);


    /* attach a client: */
    ok ((f_attach = flux_rpc_pack (h, "pty", 0, FLUX_RPC_STREAMING,
                                   "{s:s s:s s:{s:i s:i}}",
                                   "type", "attach",
                                   "mode", "rw",
                                   "winsize",
                                      "rows", 25,
                                      "cols", 80)) != NULL,
        "request: type attach");

    const char *type = NULL;
    ok (flux_rpc_get_unpack (f_attach, "{s:s}", "type", &type) == 0,
        "response: OK errno=%s", strerror (errno));
    is (type, "attach",
        "response: type=attach");
    flux_future_reset (f_attach);


    /* attach client again should fail: */
    ok ((f = flux_rpc_pack (h, "pty", 0, FLUX_RPC_STREAMING,
                                   "{s:s s:s s:{s:i s:i}}",
                                   "type", "attach",
                                   "mode", "rw",
                                   "winsize",
                                      "rows", 25,
                                      "cols", 80)) != NULL,
        "request: type attach from same client");

    ok (flux_rpc_get (f, NULL) < 0 && errno == EEXIST,
        "response: EEXIST");
    flux_future_destroy (f);


   /* resize from attached client, invalid size */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s s:{s:i s:i}}",
                            "type", "resize",
                            "winsize",
                               "rows", 0,
                               "cols", 0)) != NULL,
        "request: type resize, invalid winsize {0, 0}");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EINVAL,
        "response: EINVAL");
    flux_future_destroy (f);


   /* resize from attached client, valid size */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s s:{s:i s:i}}",
                            "type", "resize",
                            "winsize",
                               "rows", 25,
                               "cols", 80)) != NULL,
        "request: type resize, valid winsize {25, 80}");
    ok (flux_rpc_get (f, NULL) == 0,
        "response: OK");
    flux_future_destroy (f);


    /* write from attached client, invalid message */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s s:s}",
                            "type", "data",
                            "foo", "")) != NULL,
        "request: type data, invalid payload");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);


    /* write from attached client, invalid data */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s s:i}",
                            "type", "data",
                            "data", 2)) != NULL,
        "request: type data, invalid data type");
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "response: EPROTO");
    flux_future_destroy (f);


    /* invalid msg type from attached client */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s}",
                            "type", "foo")) != NULL,
        "request: type invalid");
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOSYS,
        "response: ENOSYS");
    flux_future_destroy (f);


    /* invalid msg type from attached client */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s}",
                            "type", "foo")) != NULL,
        "request: type invalid");
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOSYS,
        "response: ENOSYS");
    flux_future_destroy (f);


    /* detach client */
    ok ((f = flux_rpc_pack (h, "pty", 0, 0,
                            "{s:s}",
                            "type", "detach")) != NULL,
        "request: type invalid");
    ok (flux_rpc_get (f, NULL) == 0,
        "response: OK");
    flux_future_destroy (f);

    const char *message = NULL;
    ok (flux_rpc_get_unpack (f_attach,
                             "{s:s s:s}",
                             "type", &type,
                             "message", &message) == 0,
        "response to attach multi-response rpc");
    is (type, "exit",
        "response: type = exit");
    is (message, "Client requested detach",
        "response: message = 'Client requested detach'");
    flux_future_reset (f_attach);

    ok (flux_rpc_get (f_attach, NULL) < 0 && errno == ENODATA,
        "response: ENODATA");
    flux_future_destroy (f_attach);

    test_server_stop (h);
    flux_close (h);
}

static void pty_exit_cb (struct flux_pty_client *c, void *arg)
{
    flux_t *h = arg;
    flux_reactor_stop (flux_get_reactor (h));
}

static void test_client (void)
{
    flux_t *h = test_server_create (0, pty_server, NULL);
    flux_future_t *f = NULL;
    int rc;
    int flags = FLUX_PTY_CLIENT_ATTACH_SYNC
                | FLUX_PTY_CLIENT_NORAW;

    struct flux_pty_client *c = flux_pty_client_create ();
    if (!c)
        BAIL_OUT ("flux_pty_client_create failed");

    ok (flux_pty_client_get_flags (c) == 0,
        "initial pty client flags are 0");
    ok (flux_pty_client_set_flags (c, -1) < 0 && errno == EINVAL,
        "flux_pty_client_set_flags with invalid flags returns EINVAL");
    ok (flux_pty_client_set_flags (c, flags) == 0,
        "set client flags");

    ok (flux_pty_client_notify_exit (c, pty_exit_cb, h) == 0,
        "flux_pty_client_notify_exit");

    ok (flux_pty_client_attached (c) == false,
        "flux_pty_client_attached is false");

    ok (flux_pty_client_attach (c, h, 0, "pty") == 0,
        "flux_pty_client_attach");

    ok (flux_pty_client_attached (c),
        "flux_pty_client_attached is true after synchronous attach");

    bool on_apple = false;
    #ifdef __APPLE__
    on_apple = true;
    #endif
    // MacOS does not allow writes to the leader end of a PTY pair that doesn't
    // have an _open_ follower FD. I've tried to figure out how to make that
    // happen here, but failed.
    skip(on_apple, 4)
    f = flux_pty_client_write (c, "foo\r", 4);
    ok (f != NULL,
        "flux_pty_client_write");
    rc = flux_future_get (f, NULL);
    ok (rc == 0,
        "flux_pty_client_write: %s",
        rc == 0 ? "Success" : strerror (errno));
    flux_future_destroy (f);

    f = flux_pty_client_write (c, "bar\0\r\n", 6);
    ok (f != NULL,
        "flux_pty_client_write with U+0000");
    ok (rc == 0,
        "flux_pty_client_write: %s",
        rc == 0 ? "Success" : strerror (errno));
    flux_future_destroy (f);
    end_skip;

    ok (flux_pty_client_detach (c) == 0,
        "flux_pty_client_detach");

    /* Run reactor until pty client exits */
    flux_reactor_run (flux_get_reactor (h), 0);

    test_server_stop (h);
    flux_pty_client_destroy (c);
    flux_close (h);
}

static void monitor_cb (struct flux_pty *pty, void *data, int len)
{
    int *totalp = flux_pty_aux_get (pty, "total");
    *totalp += len;
    diag ("monitor_cb got %ld bytes", len);
}

void test_monitor ()
{
    int total = 0;
    flux_t *h = flux_open ("loop://", 0);
    struct flux_pty *pty = flux_pty_open ();

    if (!h || !pty)
        BAIL_OUT ("Unable to create test handle and pty");

    ok (flux_pty_set_flux (pty, h) == 0,
        "flux_pty_set_flux");

    ok (flux_pty_aux_set (pty, "total", &total, NULL) == 0,
        "flux_pty_aux_set");

    diag ("starting pty monitor");
    flux_pty_monitor (pty, monitor_cb);

    pty_client_send_data (pty, "hello", 6);
    pty_client_send_data (pty, "world", 6);

    ok (total == 12,
        "monitor received 12 bytes");

    flux_pty_destroy (pty);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_invalid_args ();
    test_empty_server ();
    test_basic_protocol ();
    test_client ();
    test_monitor ();
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
