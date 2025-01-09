/************************************************************  \
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
#include <jansson.h>

#include <flux/core.h>
#include "src/common/libterminus/terminus.h"
#include "src/common/libterminus/pty.h"

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

static void test_invalid_args (void)
{
    ok (flux_terminus_server_create (NULL, NULL) == NULL && errno == EINVAL,
        "flux_terminus_server_create with NULL args returns EINVAL");

    lives_ok ({flux_terminus_server_destroy (NULL);},
        "flux_terminus_server_destroy (NULL) does nothing");
    lives_ok ({flux_terminus_server_set_log (NULL, NULL, NULL);},
        "flux_terminus_server_set_log (NULL) does nothing");

    ok (flux_terminus_server_notify_empty (NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_terminus_server_notify_empty returns EINVAL on NULL args");

    ok (flux_terminus_server_session_open (NULL, 0, NULL) == NULL,
        "flux_terminus_server_session_open with NULL args returns NULL");
    ok (flux_terminus_server_session_close (NULL, NULL, 0) == -1
        && errno == EINVAL,
        "flux_terminus_server_session_close with NULL args returns EINVAL");
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
    char buf [4096];
    int len = sizeof (buf);
    if (vsnprintf (buf, len, fmt, ap) >= len) {
        buf[len-1] = '\0';
        buf[len-2] = '+';
    }
    diag ("%s:%d %s(): %s", file, line, func, buf);
}

static int terminus_server (flux_t *h, void *arg)
{
    int rc = -1;
    struct flux_terminus_server *t;

    t = flux_terminus_server_create (h, "terminus");
    if (!t)
        BAIL_OUT ("flux_terminus_server_create");

    flux_terminus_server_set_log (t, tap_logger, NULL);

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    flux_terminus_server_destroy (t);
    //flux_close (h);
    return rc;
}

static void test_kill_server_empty (void)
{
    int rc;
    flux_future_t *f = NULL;
    flux_t *h = test_server_create (0, terminus_server, NULL);

    /* kill-server
     */
    f = flux_rpc_pack (h, "terminus.kill-server", 0, 0, "{}");
    ok (f != NULL,
        "terminus.kill-server");
    rc = flux_rpc_get (f, NULL);
    ok (rc == 0,
        "terminus.kill-server: OK");
    flux_future_destroy (f);

    /* list, now fails
     */
    f = flux_rpc_pack (h, "terminus.list", 0, 0, "{}");
    ok (f != NULL,
        "terminus.list");
    rc = flux_rpc_get (f, NULL);
    ok (rc < 0 && errno == ENOSYS,
        "terminus.list: ENOSYS");
    flux_future_destroy (f);

    test_server_stop (h);
    flux_close (h);
}

static void test_protocol (void)
{
    int rc;
    json_t *o = NULL;
    flux_future_t *f = NULL;
    flux_t *h = test_server_create (0, terminus_server, NULL);

    const char *service = NULL;
    const char *name = NULL;
    const char *message = NULL;
    const char *type = NULL;
    int rank = -1;
    int status = -1;
    int id;
    double ctime;

    /* list, no sessions
     */
    f = flux_rpc_pack (h, "terminus.list", 0, 0, "{}");
    ok (f != NULL,
        "terminus.list");
    rc = flux_rpc_get_unpack (f,
                              "{s:{s:s s:i s:f} s:[]}",
                              "server",
                                 "service", &service,
                                 "rank", &rank,
                                 "ctime", &ctime,
                              "sessions");
    ok (rc == 0,
        "terminus.list: OK");
    is (service, "terminus",
        "terminus.list returned service = terminus");
    ok (rank == -1,
        "terminus.list returned expected rank");
    flux_future_destroy (f);


    /* new, add a session, invalid proto
     */
    f = flux_rpc_pack (h, "terminus.new",
                       0, 0,
                      "{s:s}",
                      "cmd", "/bin/bash");
    ok (f != NULL,
        "terminus.new: invalid proto");
    errno = 0;
    rc = flux_rpc_get_unpack (f, NULL);
    ok (rc < 0 && errno == EPROTO,
        "terminus.new (invalid proto): %s", strerror (errno));
    flux_future_destroy (f);


    /* new, add a session, no args
     */
    f = flux_rpc_pack (h, "terminus.new", 0, 0, "{}");
    ok (f != NULL,
        "terminus.new: no args");
    errno = 0;
    name = NULL;
    service = NULL;
    rc = flux_rpc_get_unpack (f,
                              "{s:s s:s s:i}",
                              "name", &name,
                              "pty_service", &service,
                              "id", &id);
    ok (rc == 0,
        "terminus.new (no args): %s", strerror (errno));
    is (name, getenv ("SHELL"),
        "terminus.new (no args): name is %s", getenv ("SHELL"));
    ok (id == 0,
        "terminus.new (no args): id is 0");
    is (service, "terminus.0",
        "terminus.new (no args): service is terminus.0");
    flux_future_destroy (f);


    /* new, add a session, full args
     */
    errno = 0;
    f = flux_rpc_pack (h,
                       "terminus.new",
                        0, 0,
                        "{s:s s:[ss] s:{s:s s:s}}",
                        "name", "test-name",
                        "cmd", "sleep", "1000",
                        "environ",
                           "PATH", "/bin:/usr/bin",
                           "HOME", "/home/user1");
    ok (f != NULL,
        "terminus.new: full args");
    service = NULL;
    name = NULL;
    errno = 0;
    rc = flux_rpc_get_unpack (f,
                              "{s:s s:s s:i}",
                              "name", &name,
                              "pty_service", &service,
                              "id", &id);
    ok (rc == 0,
        "terminus.new (full args): %s", future_strerror (f, errno));
    is (name, "test-name",
        "terminus.new (full args): name is %s", "test-name");
    ok (id == 1,
        "terminus.new (full args): id is 1");
    is (service, "terminus.1",
        "terminus.new (full args): service is terminus.1");
    flux_future_destroy (f);


    /* new, add a session, cmd only
     */
    f = flux_rpc_pack (h,
                       "terminus.new",
                        0, 0,
                        "{s:[ss]}",
                        "cmd", "sleep", "1000");
    ok (f != NULL,
        "terminus.new: cmd only");
    rc = flux_rpc_get_unpack (f,
                              "{s:s s:s s:i}",
                              "name", &name,
                              "pty_service", &service,
                              "id", &id);
    ok (rc == 0,
        "terminus.new (cmd only): OK");
    is (name, "sleep",
        "terminus.new (cmd only): name is %s", "sleep");
    ok (id == 2,
        "terminus.new (cmd only): id is 2");
    is (service, "terminus.2",
        "terminus.new (cmd only): service is terminus.2");
    flux_future_destroy (f);


    /* list, 3 sessions
     */
    f = flux_rpc_pack (h, "terminus.list", 0, 0, "{}");
    ok (f != NULL,
        "terminus.list");
    service = NULL;
    name = NULL;
    errno = 0;
    rc = flux_rpc_get_unpack (f,
                              "{s:{s:s s:i s:f} s:o}",
                              "server",
                                 "service", &service,
                                 "rank", &rank,
                                 "ctime", &ctime,
                              "sessions", &o);
    ok (rc == 0,
        "terminus.list: OK");
    is (service, "terminus",
        "terminus.list returned service = terminus");
    ok (rank == -1,
        "terminus.list returned expected rank");
    ok (o != NULL && json_is_array (o),
        "terminus.list returned sessions list");
    ok (json_array_size (o) == 3,
        "terminus.list returned 3 sessions");
    flux_future_destroy (f);


    /* kill session
     */
    f = flux_rpc_pack (h,
                       "terminus.kill",
                       0, FLUX_RPC_STREAMING,
                       "{s:i s:i s:i}",
                       "id", 0,
                       "signal", SIGKILL,
                       "wait", 1);
    ok (f != NULL,
        "terminus.kill (wait)");
    errno = 0;
    rc = flux_rpc_get_unpack (f,
                              "{s:s s:s s:i}",
                              "type", &type,
                              "message", &message,
                              "status", &status);
    ok (rc == 0,
        "terminus.kill (wait): OK");
    is (type, "exit",
        "terminus.kill (wait): response is of type exit");
    ok (status == 0x9,
        "terminus.kill (wait): status == 0x9");
    flux_future_reset (f);
    rc = flux_rpc_get (f, NULL);
    ok (rc < 0 && errno == ENODATA,
        "terminus.kill (wait): ENODATA (end of streaming response)");
    flux_future_destroy (f);


    /* kill: invalid session
     */
    f = flux_rpc_pack (h,
                       "terminus.kill",
                       0, 0,
                       "{s:i s:i}",
                       "id", 0,
                       "signal", SIGKILL);
    ok (f != NULL,
        "terminus.kill (invalid session)");
    errno = 0;
    rc = flux_rpc_get (f, NULL);
    ok (rc == -1 && errno == ENOENT,
        "terminus.kill: ENOENT (got %s)", strerror (errno));
    flux_future_destroy (f);


    /* list, 2 sessions
     */
    f = flux_rpc_pack (h, "terminus.list", 0, 0, "{}");
    ok (f != NULL,
        "terminus.list");
    service = NULL;
    name = NULL;
    errno = 0;
    o = NULL;
    rc = flux_rpc_get_unpack (f,
                              "{s:{s:s s:i s:f} s:o !}",
                              "server",
                                 "service", &service,
                                 "rank", &rank,
                                 "ctime", &ctime,
                              "sessions", &o);
    ok (rc == 0,
        "terminus.list: OK");
    ok (o && json_is_array (o) && json_array_size (o) == 2,
        "terminus.list: now returns 2 sessions");
    flux_future_destroy (f);


    /* kill session (no wait)
     */
    f = flux_rpc_pack (h,
                       "terminus.kill",
                       0, 0,
                       "{s:i s:i}",
                       "id", 1,
                       "signal", SIGKILL);
    ok (f != NULL,
        "terminus.kill (no wait)");
    errno = 0;
    rc = flux_rpc_get (f, NULL);
    ok (rc == 0,
        "terminus.kill (no wait): OK");
    flux_future_destroy (f);


    /* kill-server
     */
    f = flux_rpc_pack (h, "terminus.kill-server", 0, 0, "{}");
    ok (f != NULL,
        "terminus.kill-server");
    errno = 0;
    rc = flux_rpc_get (f, NULL);
    ok (rc == 0,
        "terminus.kill-server: OK (%s)",
        rc == 0 ? "Success": strerror (errno));
    flux_future_destroy (f);


    /* list, now fails
     */
    f = flux_rpc_pack (h, "terminus.list", 0, 0, "{}");
    ok (f != NULL,
        "terminus.list");
    rc = flux_rpc_get (f, NULL);
    ok (rc < 0 && errno == ENOSYS,
        "terminus.list: ENOSYS");
    flux_future_destroy (f);

    test_server_stop (h);
    flux_close (h);
}

void test_open_close_session (void)
{
    int rc;
    struct flux_pty *pty = NULL;
    struct flux_pty *pty0 = NULL;
    struct flux_pty *pty1 = NULL;
    struct flux_terminus_server *t = NULL;
    struct flux_terminus_server *t2 = NULL;
    flux_t *h = flux_open ("loop://", 0);
    flux_reactor_t *r;

    if (!h || !(r = flux_get_reactor (h)))
        BAIL_OUT ("Failed to create loopback handle");

    t = flux_terminus_server_create (h, "terminus");
    ok (t != NULL,
        "flux_terminus_server_create()");
    t2 = flux_terminus_server_create (h, "terminus2");
    ok (t2 != NULL,
        "flux_terminus_server_create()");

    flux_terminus_server_set_log (t, tap_logger, NULL);
    flux_terminus_server_set_log (t2, tap_logger, NULL);

    pty = flux_terminus_server_session_open (t, -1, "test session");
    ok (pty == NULL && errno == EINVAL,
        "flux_terminus_server_session_open with invalid id returns EINVAL");
    pty = flux_terminus_server_session_open (t, 0, NULL);
    ok (pty == NULL && errno == EINVAL,
        "flux_terminus_server_session_open with NULL name returns EINVAL");

    pty0 = flux_terminus_server_session_open (t, 0, "test session");
    ok (pty0 != NULL,
        "flux_terminus_server_session_open works");

    pty1 = flux_terminus_server_session_open (t, 1, "another test session");
    ok (pty1 != NULL,
        "flux_terminus_server_session_open again works");
    rc = flux_terminus_server_session_close (t, pty1, 0);
    ok (pty1 != NULL,
        "flux_terminus_server_close");

    pty = flux_terminus_server_session_open (t, 0, "duplicate");
    ok (pty == NULL && errno == EEXIST,
        "flux_terminus_server_session_open with duplicate id returns EEXIST");

    rc = flux_terminus_server_session_close (t, NULL, 0);
    ok (rc < 0 && errno == EINVAL,
        "flux_terminus_session_close with NULL pty returns EINVAL");

    rc = flux_terminus_server_session_close (t, pty0, -1);
    ok (rc < 0 && errno == EINVAL,
        "flux_terminus_session_close with invalid status returns EINVAL");

    rc = flux_terminus_server_session_close (t, pty0, 0);
    ok (rc == 0,
        "flux_terminus_session_close works");

    pty0 = flux_terminus_server_session_open (t2, 0, "session0");
    ok (pty0 != NULL,
        "flux_terminus_server_session_open on second server");
    rc = flux_terminus_server_session_close (t, pty0, 0);
    ok (rc < 0 && errno == ENOENT,
        "flux_terminus_server_session_close wrong server returns ENOENT");
    rc = flux_terminus_server_session_close (t2, pty0, 0);
    ok (rc == 0,
        "flux_terminus_server_session_close right server works");

    flux_terminus_server_destroy (t);
    flux_terminus_server_destroy (t2);
    flux_close (h);
}


int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    /* Make sure SHELL is set in environment.
     */
    if (getenv ("SHELL") == NULL)
        setenv ("SHELL", "/bin/sh", 1);

    /*  set rank == -1 for testing. Avoid flux_get_rank(3) */
    setenv ("FLUX_TERMINUS_TEST_SERVER", "t", 1);

    test_invalid_args ();
    test_kill_server_empty ();
    test_protocol ();
    test_open_close_session ();
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
