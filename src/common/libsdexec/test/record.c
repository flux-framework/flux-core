/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libeventlog/eventlog.h"
#include "record.h"
#include "unit.h"

static char dir[1024];

/* Replace dir/name with a start event followed by 'tail', which stands in
 * for whatever ExecStopPost appended.
 */
static void write_exit (const char *name, const char *tail)
{
    char path[2048];
    FILE *f;
    json_t *o;

    if (!(o = json_pack ("{s:s}", "label", "x")) || sdexec_record_write (dir, name, o) < 0)
        BAIL_OUT ("could not write record");
    json_decref (o);
    snprintf (path, sizeof (path), "%s/%s", dir, name);
    if (!(f = fopen (path, "a")) || fputs (tail, f) == EOF || fclose (f))
        BAIL_OUT ("could not append to %s", path);
}

/* An exit event as ExecStopPost writes it.
 */
#define EXIT_EVENT(result, code, status) \
    "{\"timestamp\":1.5,\"name\":\"exit\",\"context\":" \
    "{\"result\":\"" result "\",\"code\":\"" code "\"," \
    "\"status\":\"" status "\"}}\n"

static bool file_exists (const char *name, const char *ext)
{
    char path[2048];
    struct stat sb;

    snprintf (path, sizeof (path), "%s/%s%s", dir, name, ext);
    return stat (path, &sb) == 0;
}

/* Run the ExecStopPost command line the way systemd would, with the
 * result variables in the environment.  Return its exit code.
 */
static int run_execstoppost (const char *argv0,
                             const char *argv1,
                             const char *argv2,
                             const char *argv3,
                             const char *argv4)
{
    pid_t pid;
    int wstatus;

    switch ((pid = fork ())) {
        case -1:
            BAIL_OUT ("fork failed");
        case 0:
            setenv ("SERVICE_RESULT", "exit-code", 1);
            setenv ("EXIT_CODE", "exited", 1);
            setenv ("EXIT_STATUS", "42", 1);
            execv (argv0, (char *const[]){ (char *)argv0,
                                           (char *)argv1,
                                           (char *)argv2,
                                           (char *)argv3,
                                           (char *)argv4,
                                           NULL });
            _exit (127);
        default:
            break;
    }
    if (waitpid (pid, &wstatus, 0) != pid || !WIFEXITED (wstatus))
        return -1;
    return WEXITSTATUS (wstatus);
}

/* Decode dir/name with libeventlog and return the timestamps of its first
 * two events, which must be "start" and "exit".
 */
static bool eventlog_ok (const char *name, double *ts1, double *ts2)
{
    char path[2048];
    int fd;
    void *buf = NULL;
    json_t *a = NULL;
    const char *n1, *n2;
    bool rc = false;

    snprintf (path, sizeof (path), "%s/%s", dir, name);
    if ((fd = open (path, O_RDONLY)) < 0)
        return false;
    if (read_all (fd, &buf) >= 0
        && (a = eventlog_decode (buf))
        && json_array_size (a) == 2
        && eventlog_entry_parse (json_array_get (a, 0), ts1, &n1, NULL) == 0
        && eventlog_entry_parse (json_array_get (a, 1), ts2, &n2, NULL) == 0
        && streq (n1, "start")
        && streq (n2, "exit"))
        rc = true;
    close (fd);
    free (buf);
    json_decref (a);
    return rc;
}

void test_basic (void)
{
    const char *name = "foo:0-inst.service";
    json_t *o;
    json_t *o2;
    json_t *a;
    const char *s;
    int i;

    if (!(o = json_pack ("{s:s s:i}", "label", "foo", "waitable", 1)))
        BAIL_OUT ("could not create record object");

    errno = 0;
    ok (sdexec_record_read (dir, name) == NULL && errno == ENOENT,
        "sdexec_record_read on missing record fails with ENOENT");
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == ENOENT,
        "sdexec_record_read_exit on missing exit fails with ENOENT");
    ok (sdexec_record_remove (dir, name) == 0,
        "sdexec_record_remove on missing record succeeds");

    ok (sdexec_record_write (dir, name, o) == 0,
        "sdexec_record_write works");
    ok (file_exists (name, "") && !file_exists (name, ".tmp"),
        "record exists and temporary file does not");
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == ENOENT,
        "sdexec_record_read_exit on record with no exit fails with ENOENT");
    ok ((o2 = sdexec_record_read (dir, name)) != NULL
        && json_unpack (o2, "{s:s s:i}", "label", &s, "waitable", &i) == 0
        && streq (s, "foo")
        && i == 1,
        "sdexec_record_read returns what was written");
    json_decref (o2);

    ok ((a = sdexec_record_list (dir, "")) != NULL
        && json_array_size (a) == 1
        && streq (json_string_value (json_array_get (a, 0)), name),
        "sdexec_record_list suffix=\"\" lists the record");
    json_decref (a);
    ok ((a = sdexec_record_list (dir, ":0-inst.service")) != NULL
        && json_array_size (a) == 1,
        "sdexec_record_list with matching suffix lists the record");
    json_decref (a);
    ok ((a = sdexec_record_list (dir, ":1-inst.service")) != NULL
        && json_array_size (a) == 0,
        "sdexec_record_list with other suffix lists nothing");
    json_decref (a);

    ok (sdexec_record_remove (dir, name) == 0,
        "sdexec_record_remove works");
    ok (!file_exists (name, ""),
        "record is gone");

    json_decref (o);
}

void test_exit (void)
{
    const char *name = "bar:inst.service";
    struct unit *unit;
    json_t *dict;
    json_t *o;
    int status;

    /* exit 0 */
    write_exit (name, EXIT_EVENT ("success", "exited", "0"));
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL,
        "sdexec_record_read_exit works for exit 0");
    if (!(unit = sdexec_unit_create (name)))
        BAIL_OUT ("could not create unit");
    ok (sdexec_unit_update (unit, dict) == true
        && sdexec_unit_has_finished (unit)
        && (status = sdexec_unit_wait_status (unit)) >= 0
        && WIFEXITED (status)
        && WEXITSTATUS (status) == 0,
        "unit updated from exit record has wait status exit 0");
    sdexec_unit_destroy (unit);
    json_decref (dict);

    /* exit 7 */
    write_exit (name, EXIT_EVENT ("exit-code", "exited", "7"));
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL,
        "sdexec_record_read_exit works for exit 7");
    if (!(unit = sdexec_unit_create (name)))
        BAIL_OUT ("could not create unit");
    ok (sdexec_unit_update (unit, dict) == true
        && (status = sdexec_unit_wait_status (unit)) >= 0
        && WIFEXITED (status)
        && WEXITSTATUS (status) == 7,
        "unit updated from exit record has wait status exit 7");
    sdexec_unit_destroy (unit);
    json_decref (dict);

    /* killed by SIGKILL (systemd spells the signal without SIG) */
    write_exit (name, EXIT_EVENT ("signal", "killed", "KILL"));
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL,
        "sdexec_record_read_exit works for signal death");
    if (!(unit = sdexec_unit_create (name)))
        BAIL_OUT ("could not create unit");
    ok (sdexec_unit_update (unit, dict) == true
        && (status = sdexec_unit_wait_status (unit)) >= 0
        && WIFSIGNALED (status)
        && WTERMSIG (status) == SIGKILL,
        "unit updated from exit record has wait status SIGKILL");
    sdexec_unit_destroy (unit);
    json_decref (dict);

    /* core dump */
    write_exit (name, EXIT_EVENT ("core-dump", "dumped", "SEGV"));
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL,
        "sdexec_record_read_exit works for core dump");
    if (!(unit = sdexec_unit_create (name)))
        BAIL_OUT ("could not create unit");
    ok (sdexec_unit_update (unit, dict) == true
        && (status = sdexec_unit_wait_status (unit)) >= 0
        && WIFSIGNALED (status)
        && WTERMSIG (status) == SIGSEGV,
        "unit updated from exit record has wait status SIGSEGV");
    sdexec_unit_destroy (unit);
    json_decref (dict);

    /* other events are ignored, and only the first exit event counts */
    write_exit (name,
                "{\"timestamp\":1.0,\"name\":\"adopted\"}\n"
                EXIT_EVENT ("exit-code", "exited", "9")
                EXIT_EVENT ("success", "exited", "0"));
    if (!(unit = sdexec_unit_create (name)))
        BAIL_OUT ("could not create unit");
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL
        && sdexec_unit_update (unit, dict) == true
        && (status = sdexec_unit_wait_status (unit)) >= 0
        && WIFEXITED (status)
        && WEXITSTATUS (status) == 9,
        "sdexec_record_read_exit skips other events, takes first exit");
    sdexec_unit_destroy (unit);
    json_decref (dict);

    /* damage */
    write_exit (name, "{\"timestamp\":1.5,\"name\":\"ex");
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read_exit on truncated exit event fails with EPROTO");
    ok ((o = sdexec_record_read (dir, name)) != NULL,
        "sdexec_record_read still reads the start event");
    json_decref (o);
    write_exit (name, "garbage\n");
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read_exit on undecodable line fails with EPROTO");
    write_exit (name, EXIT_EVENT ("success", "exited", ""));
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read_exit on empty status fails with EPROTO");
    write_exit (name, EXIT_EVENT ("success", "exited", "notanumber"));
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read_exit on bad status fails with EPROTO");
    write_exit (name, EXIT_EVENT ("success", "frobnicated", "0"));
    errno = 0;
    ok (sdexec_record_read_exit (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read_exit on bad code fails with EPROTO");

    ok (sdexec_record_remove (dir, name) == 0, "sdexec_record_remove works");
    ok (!file_exists (name, ""), "record is gone");

    /* a record whose first event is not start */
    {
        char path[2048];
        FILE *f;
        snprintf (path, sizeof (path), "%s/%s", dir, name);
        if (!(f = fopen (path, "w"))
            || fputs (EXIT_EVENT ("success", "exited", "0"), f) == EOF
            || fclose (f))
            BAIL_OUT ("could not write %s", path);
    }
    errno = 0;
    ok (sdexec_record_read (dir, name) == NULL && errno == EPROTO,
        "sdexec_record_read on record with no start event fails with EPROTO");
    ok (sdexec_record_remove (dir, name) == 0,
        "sdexec_record_remove works");
}

void test_tmp (void)
{
    const char *names[] = {
        "a:0-inst.service.tmp",
        "b:1-inst.service.tmp",
        "c:0-inst.service",
    };
    json_t *a;
    char path[2048];
    int i;

    for (i = 0; i < 3; i++) {
        FILE *f;
        snprintf (path, sizeof (path), "%s/%s", dir, names[i]);
        if (!(f = fopen (path, "w")) || fclose (f))
            BAIL_OUT ("could not create %s", path);
    }
    ok ((a = sdexec_record_list (dir, "")) != NULL
        && json_array_size (a) == 1
        && streq (json_string_value (json_array_get (a, 0)), names[2]),
        "sdexec_record_list does not list temporary files");
    json_decref (a);
    ok (sdexec_record_remove_tmp (dir, ":0-inst.service") == 1,
        "sdexec_record_remove_tmp removes one matching temporary file");
    ok (!file_exists (names[0], "")
        && file_exists (names[1], "")
        && file_exists (names[2], ""),
        "only the matching temporary file was removed");
    ok (sdexec_record_remove_tmp (dir, "") == 1
        && !file_exists (names[1], ""),
        "sdexec_record_remove_tmp suffix=\"\" removes the other");
    ok (sdexec_record_remove (dir, names[2]) == 0,
        "sdexec_record_remove works");
}

/* Run the ExecStopPost command line the way systemd would, with the
 * result variables in the environment, and check the exit file it writes.
 */
void test_execstoppost (void)
{
    const char *name = "baz:inst.service";
    json_t *a;
    const char *argv0, *argv1, *argv2, *argv3, *argv4;
    json_t *dict;
    const char *t1, *t2;
    int code, status;
    json_t *o;
    double ts1, ts2;

    ok ((a = sdexec_record_execstoppost (dir, name)) != NULL
        && json_unpack (a,
                        "[sssss]",
                        &argv0,
                        &argv1,
                        &argv2,
                        &argv3,
                        &argv4) == 0
        && streq (argv0, "/bin/sh")
        && streq (argv1, "-c")
        && streq (argv3, "sh")
        && strends (argv4, "/baz:inst.service"),
        "sdexec_record_execstoppost returns sh -c script sh path");

    /* With no record, the command does not create one. */
    ok (run_execstoppost (argv0, argv1, argv2, argv3, argv4) == 0,
        "running the ExecStopPost command with no record works");
    ok (!file_exists (name, ""),
        "and it did not create a record");

    if (!(o = json_pack ("{s:s}", "label", "baz"))
        || sdexec_record_write (dir, name, o) < 0)
        BAIL_OUT ("could not write record");
    json_decref (o);

    ok (run_execstoppost (argv0, argv1, argv2, argv3, argv4) == 0,
        "running the ExecStopPost command works");
    ok ((dict = sdexec_record_read_exit (dir, name)) != NULL
        && json_unpack (dict,
                        "{s:[si] s:[si]}",
                        "ExecMainCode", &t1, &code,
                        "ExecMainStatus", &t2, &status) == 0
        && code == CLD_EXITED
        && status == 42,
        "exit event it appended parses as exit 42");
    json_decref (dict);
    ok ((o = sdexec_record_read (dir, name)) != NULL
        && json_unpack (o, "{s:s}", "label", &t1) == 0
        && streq (t1, "baz"),
        "start event is intact after the append");
    json_decref (o);
    ok (eventlog_ok (name, &ts1, &ts2) && ts1 > 0 && ts2 >= ts1,
        "record is a valid eventlog with increasing timestamps");
    json_decref (a);

    ok (sdexec_record_remove (dir, name) == 0,
        "sdexec_record_remove works");
}

void test_inval (void)
{
    json_t *o;

    if (!(o = json_object ()))
        BAIL_OUT ("could not create object");

    errno = 0;
    ok (sdexec_record_write (NULL, "x", o) < 0 && errno == EINVAL,
        "sdexec_record_write dir=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_write (dir, NULL, o) < 0 && errno == EINVAL,
        "sdexec_record_write name=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_write (dir, "", o) < 0 && errno == EINVAL,
        "sdexec_record_write name=\"\" fails with EINVAL");
    errno = 0;
    ok (sdexec_record_write (dir, "a/b", o) < 0 && errno == EINVAL,
        "sdexec_record_write name with slash fails with EINVAL");
    errno = 0;
    ok (sdexec_record_write (dir, "..", o) < 0 && errno == EINVAL,
        "sdexec_record_write name=.. fails with EINVAL");
    errno = 0;
    ok (sdexec_record_write (dir, "x", NULL) < 0 && errno == EINVAL,
        "sdexec_record_write context=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_remove_tmp (NULL, "") < 0 && errno == EINVAL,
        "sdexec_record_remove_tmp dir=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_read (NULL, "x") == NULL && errno == EINVAL,
        "sdexec_record_read dir=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_read_exit (dir, NULL) == NULL && errno == EINVAL,
        "sdexec_record_read_exit name=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_remove (NULL, "x") < 0 && errno == EINVAL,
        "sdexec_record_remove dir=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_list (dir, NULL) == NULL && errno == EINVAL,
        "sdexec_record_list suffix=NULL fails with EINVAL");
    errno = 0;
    ok (sdexec_record_list ("/nonexistent/dir", "") == NULL
        && errno == ENOENT,
        "sdexec_record_list on missing dir fails with ENOENT");
    errno = 0;
    ok (sdexec_record_execstoppost (dir, NULL) == NULL && errno == EINVAL,
        "sdexec_record_execstoppost name=NULL fails with EINVAL");

    json_decref (o);
}

int main (int ac, char *av[])
{
    const char *tmpdir = getenv ("TMPDIR");

    plan (NO_PLAN);

    snprintf (dir, sizeof (dir), "%s/record-test.XXXXXX", tmpdir ? tmpdir : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp failed");

    test_basic ();
    test_exit ();
    test_execstoppost ();
    test_tmp ();
    test_inval ();

    if (rmdir (dir) < 0)
        diag ("rmdir %s: %s", dir, strerror (errno));

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
