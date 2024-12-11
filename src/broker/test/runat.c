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

#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/stdlog.h"

#include "src/broker/runat.h"

static zlist_t *logs;

struct context {
    flux_t *h;
};

void clear_list (zlist_t *list)
{
    char *s;
    while ((s = zlist_pop (list)))
        free (s);
}

int match_list (zlist_t *list, const char *key)
{
    char *s;
    int count = 0;

    s = zlist_first (list);
    while (s) {
        if (strstr (s, key) != NULL)
            count++;
        s = zlist_next (list);
    }
    return count;
}

/* Just stop the reactor so we can call flux_reactor_run() and process
 * one completion.
 */
static int completion_called;
void test_completion (struct runat *r, const char *name, void *arg)
{
    struct context *ctx = arg;
    flux_reactor_stop (flux_get_reactor (ctx->h));
    completion_called++;
}

void basic (flux_t *h)
{
    struct runat *r;
    struct context ctx;
    int flags, rc;

    ctx.h = h;

    r = runat_create (h, "local://notreally", false);
    ok (r != NULL,
        "runat_create works");

    /* run true;true */
    clear_list (logs);
    ok (runat_is_defined (r, "test1") == false,
        "runat_is_defined name=test1 returns false");
    ok (runat_is_completed (r, "test1") == false,
        "runat_is_completed name=test1 returns false");
    ok (runat_push_shell_command (r, "test1", "/bin/true", 0) == 0
        && runat_push_shell_command (r, "test1", "/bin/true", 0) == 0,
        "pushed true;true");
    ok (runat_is_defined (r, "test1") == true,
        "runat_is_defined name=test1 returns true after creation");
    ok (runat_is_completed (r, "test1") == false,
        "runat_is_completed returns false");
    ok (runat_start (r, "test1", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test1", &rc) == 0 && rc == 0,
        "exit code is zero");
    ok (match_list (logs, "Exited") == 2,
        "Exited was logged twice");
    ok (runat_is_completed (r, "test1") == true,
        "runat_is_completed returns true");

    /* run false;true */
    clear_list (logs);
    ok (runat_push_shell_command (r, "test2", "/bin/true", 0) == 0
        && runat_push_shell_command (r, "test2", "/bin/false", 0) == 0,
        "pushed true;true");
    ok (runat_start (r, "test2", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test2", &rc) == 0 && rc == 1,
        "exit code is 1");
    ok (match_list (logs, "rc=1") == 1
        && match_list (logs, "Exited") == 2,
        "Both commands' exit status was logged");

    /* run true;false */
    clear_list (logs);
    ok (runat_push_command (r, "test3", "/bin/false", 11, 0) == 0
        && runat_push_command (r, "test3", "/bin/true", 10, 0) == 0,
        "pushed true;true");
    ok (runat_start (r, "test3", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test3", &rc) == 0 && rc == 1,
        "exit code is 1");
    ok (match_list (logs, "rc=1") == 1
        && match_list (logs, "Exited") == 2,
        "Both commands' exit status were logged");

    /* generate output to stdout and stderr */
    clear_list (logs);
    ok (runat_push_shell_command (r,
                                  "test4",
                                  "echo test4-out",
                                  RUNAT_FLAG_LOG_STDIO) == 0
    && runat_push_shell_command (r,
                                 "test4",
                                 "echo test4-err>&2",
                                 RUNAT_FLAG_LOG_STDIO) == 0,
        "pushed echo;echo");
    ok (runat_start (r, "test4", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test4", &rc) == 0 && rc == 0,
        "exit code is 0");
    ok (match_list (logs, "Exited") == 2,
        "Both commands' exit status were logged");
    ok (match_list (logs, "info: test4.1: test4-out") == 1,
        "Stdout was logged");
    ok (match_list (logs, "err: test4.0: test4-err") == 1,
        "Stderr was logged");

    /* run notfound;echo foo*/
    clear_list (logs);
    ok (runat_push_shell_command (r,
                                  "test5",
                                  "echo test5-out",
                                  RUNAT_FLAG_LOG_STDIO) == 0
        && runat_push_shell_command (r, "test5", "notfound", 0) == 0,
        "pushed notfound;echo");
    ok (runat_start (r, "test5", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test5", &rc) == 0 && rc != 1,
        "exit code is nonzero");
    errno = 0;
    ok (match_list (logs, "notfound Exited") == 1
        && match_list (logs, "echo test5-out Exited") == 1,
        "Both commands' exit status were logged");

    /* run printenv FLUX_URI */
    clear_list (logs);
    ok (runat_push_shell_command (r,
                                  "test6",
                                  "printenv FLUX_URI",
                                  RUNAT_FLAG_LOG_STDIO) == 0,
        "pushed printenv FLUX_URI");
    ok (runat_start (r, "test6", test_completion, &ctx) == 0,
        "runat_start works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    rc = -1;
    ok (runat_get_exit_code (r, "test6", &rc) == 0 && rc == 0,
        "exit code zero");
    ok (match_list (logs, "local://notreally") == 1,
        "FLUX_URI was set for subprocess");

    /* run sleep 3600, then abort */
    /* N.B. if sleep has started, the abort function kills it.
     * If it is not yet started, the subprocess state callback kills it
     * when it transitions to running.  Either way we should see an
     * exit code indicating terminated.
     */
    clear_list (logs);
    flags = 0;
    /* older versions of glibc, POSIX_SPAWN_SETPGROUP with
     * posix_spawn(3) can be racy, and abort below can fail to kill
     * the child.  The result would be a wait for the 3600 sleep to
     * complete, giving the appearance of a hang.  If we're under an
     * old version of glibc, force the use of fork(2)/exec(2) over
     * posix_spawn(3).
     */
#if __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 24)
    flags |= RUNAT_FLAG_FORK_EXEC;
#endif
    ok (runat_push_shell_command (r, "test7", "/bin/true", flags) == 0
            && runat_push_shell_command (r, "test7", "sleep 3600", flags) == 0,
        "pushed /bin/true;sleep 3600");
    ok (runat_start (r, "test7", test_completion, &ctx) == 0,
        "runat_start works");
    ok (runat_abort (r, "test7") == 0,
        "runat_abort works");
    completion_called = 0;
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0
        && completion_called == 1,
        "completion called once");
    ok (runat_get_exit_code (r, "test7", &rc) == 0 && rc == 129,
        "exit code 129 (= signal 15 + 1)");
    ok (match_list (logs, "Hangup") == 1,
        "process termination was logged");
    rc = -1;

    runat_destroy (r);
}

void diag_logger (const char *buf, int len, void *arg)
{
    struct stdlog_header hdr;
    const char *msg;
    size_t msglen;
    int severity;
    char *s;

    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
        BAIL_OUT ("stdlog_decode failed");
    severity = STDLOG_SEVERITY (hdr.pri);
    if (asprintf (&s,
                  "%s: %.*s\n",
                  stdlog_severity_to_string (severity),
                  (int)msglen,
                  msg) < 0)
        BAIL_OUT ("asprintf failed");
    diag (s);
    if (zlist_append (logs, s) < 0)
        BAIL_OUT ("zlist_append failed");
}

void badinput (flux_t *h)
{
    struct runat *r;
    int rc;

    if (!(r = runat_create (h, NULL, false)))
        BAIL_OUT ("runat_create failed");

    ok (runat_is_defined (NULL, "foo") == false,
        "runat_is_defined r=NULL returns false");
    ok (runat_is_defined (r, NULL) == false,
        "runat_is_defined name=NULL returns false");
    ok (runat_is_completed (NULL, "foo") == false,
        "runat_is_completed r=NULL returns false");
    ok (runat_is_completed (r, NULL) == false,
        "runat_is_completed name=NULL returns false");

    errno = 0;
    ok (runat_start (NULL, "foo", NULL, NULL) < 0 && errno == EINVAL,
        "runat_start r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_start (r, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "runat_start name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_start (r, "noexit", NULL, NULL) < 0 && errno == ENOENT,
        "runat_start name=noexist fails with ENOENT");

    errno = 0;
    ok (runat_abort (NULL, "foo") < 0 && errno == EINVAL,
        "runat_abort r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_abort (r, NULL) < 0 && errno == EINVAL,
        "runat_abort name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_abort (r, "noexit") < 0 && errno == ENOENT,
        "runat_abort name=noexist fails with ENOENT");

    errno = 0;
    ok (runat_get_exit_code (NULL, "foo", &rc) < 0 && errno == EINVAL,
        "runat_get_exit_code r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_get_exit_code (r, NULL, &rc) < 0 && errno == EINVAL,
        "runat_get_exit_code name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_get_exit_code (r, "foo", NULL) < 0 && errno == EINVAL,
        "runat_get_exit_code rc=NULL fails with EINVAL");
    errno = 0;
    ok (runat_get_exit_code (r, "noexist", &rc) < 0 && errno == ENOENT,
        "runat_get_exit_code rc=NULL fails with ENOENT");

    errno = 0;
    ok (runat_push_shell (NULL, "foo", NULL, 0) < 0 && errno == EINVAL,
        "runat_push_shell r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_shell (r, NULL, NULL, 0) < 0 && errno == EINVAL,
        "runat_push_shell name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_shell (r, "foo", NULL, RUNAT_FLAG_LOG_STDIO) < 0
        && errno == EINVAL,
        "runat_push_shell flags=RUNAT_FLAG_LOG_STDIO fails with EINVAL");

    errno = 0;
    ok (runat_push_shell_command (NULL, "a", "a", 0) < 0 && errno == EINVAL,
        "runat_push_shell_command r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_shell_command (r, NULL, "a", 0) < 0 && errno == EINVAL,
        "runat_push_shell_command name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_shell_command (r, "foo", NULL, 0) < 0 && errno == EINVAL,
        "runat_push_shell_command cmdline=NULL fails with EINVAL");

    errno = 0;
    ok (runat_push_command (NULL, "a", "a", 1, 0) < 0 && errno == EINVAL,
        "runat_push_command r=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_command (r, NULL, "a", 1, 0) < 0 && errno == EINVAL,
        "runat_push_command name=NULL fails with EINVAL");
    errno = 0;
    ok (runat_push_command (r, "foo", NULL, 1, 0) < 0 && errno == EINVAL,
        "runat_push_command argz=NULL fails with EINVAL");

    runat_destroy (r);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *r;

    plan (NO_PLAN);

    /* these tests require bourne shell */
    if (setenv ("SHELL", "/bin/sh", 1) < 0)
        BAIL_OUT ("setenv set /bin/sh failed");

    if (!(logs = zlist_new ()))
        BAIL_OUT ("zlist_new failed");
    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        BAIL_OUT ("flux_reactor_create failed");
    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (flux_set_reactor (h, r) < 0)
        BAIL_OUT ("flux_set_reactor failed");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank failed");
    flux_log_set_redirect (h, diag_logger, NULL);
    flux_log (h, LOG_INFO, "test log message");

    basic (h);
    badinput (h);

    flux_reactor_destroy (r);
    flux_close (h);

    clear_list (logs);
    zlist_destroy (&logs);
    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */



