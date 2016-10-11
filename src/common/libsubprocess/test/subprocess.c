/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "src/common/libjson-c/json.h"

#include "tap.h"
#include "subprocess.h"
#include "zio.h"

extern char **environ;

static void *myfatal_h = NULL;
void myfatal (void *h, int exit_code, const char *fmt, ...)
{
    myfatal_h = h;
}

static int testio_cb (struct subprocess *p, const char *json_str);
static int fdcount (void);

int main (int ac, char **av)
{
    int rc;
    struct subprocess_manager *sm;
    struct subprocess *p, *q;
    const char *s;
    char *buf;
    char *args[] = { "hello", NULL };
    char *args2[] = { "goodbye", NULL };
    char *args3[] = { "/bin/true", NULL };
    char *args4[] = { "/bin/sleep", "10", NULL };
    int start_fdcount, end_fdcount;

    start_fdcount = fdcount ();
    diag ("initial fd count %d", start_fdcount);

    plan (NO_PLAN);

    if (!(sm = subprocess_manager_create ()))
        BAIL_OUT ("Failed to create subprocess manager");
    ok (sm != NULL, "create subprocess manager");

    diag ("subprocess accessors tests");
    if (!(p = subprocess_create (sm)))
        BAIL_OUT ("Failed to create subprocess handle: %s", strerror (errno));
    ok (p != NULL, "create subprocess handle");

    rc = subprocess_set_args (p, 1, args);
    ok (rc >= 0, "subprocess_set_args: %s", strerror (errno));

    ok (subprocess_get_argc (p) == 1, "subprocess argc is 1");

    s = subprocess_get_arg (p, 0);
    is (s, "hello", "subprocess argv[0] is 'hello'");

    rc = subprocess_argv_append (p, "foo");
    ok (rc >= 0, "subprocess_arg_append");
    ok (subprocess_get_argc (p) == 2, "subprocess argc is now 2");

    s = subprocess_get_arg (p, 2);
    ok (s == NULL, "subprocess_get_arg() out of bounds returns NULL");

    rc = subprocess_set_args (p, 1, args2);
    ok (rc >= 0, "set_args replaces existing");

    s = subprocess_get_arg (p, 0);
    is (s, "goodbye", "subprocess argv[0] is 'goodbye'");

    rc = subprocess_setenv (p, "FOO", "bar", 1);
    ok (rc >= 0, "subprocess_setenv");

    s = subprocess_getenv (p, "FOO");
    is (s, "bar", "subprocess_getenv works");

    rc = subprocess_setenv (p, "FOO", "bar2", 0);
    ok (rc == -1, "subprocess_setenv without overwrite fails for existing var");
    ok (errno == EEXIST, "and with appropriate errno");

    s = subprocess_getenv (p, "FOO");
    is (s, "bar", "subproces_getenv still shows correct variable");

    subprocess_unsetenv (p, "FOO");
    s = subprocess_getenv (p, "FOO");
    ok (s == NULL, "subproces_getenv fails for unset variable");

    rc = subprocess_setenvf (p, "FOO", 1, "%d", 42);
    ok (rc >= 0, "subprocess_setenvf");

    s = subprocess_getenv (p, "FOO");
    is (s, "42", "subprocess_getenv works after setenvf");

    is (subprocess_state_string (p), "Pending",
        "Unstarted process has state 'Pending'");

    subprocess_destroy (p);
    diag ("fd count after subproc create/destroy %d", fdcount ());

    /* Test running an executable */
    diag ("test subprocess_manager_run");
    p = subprocess_manager_run (sm, 1, args3, NULL);
    ok (p != NULL, "subprocess_manager_run");
    ok (subprocess_pid (p) != (pid_t) -1, "process has valid pid");

    q = subprocess_manager_wait (sm);
    ok (p == q, "subprocess_manager_wait returns correct process");

    ok (subprocess_exited (p), "subprocess has exited after wait returns");
    is (subprocess_state_string (p), "Exited", "State is now 'Exited'");
    ok (subprocess_exit_code (p) == 0, "With expected exit code");
    subprocess_destroy (p);
    q = NULL;

    /*  Test failing program */
    diag ("test expected failure from subprocess_manager_run");
    args3[0] = "/bin/false";
    p = subprocess_manager_run (sm, 1, args3, NULL);
    if (p) {
        ok (p != NULL, "subprocess_manager_run");
        ok (subprocess_pid (p) != (pid_t) -1, "process has valid pid");
        q = subprocess_manager_wait (sm);
        ok (p == q, "subprocess_manager_wait returns correct process");
        is (subprocess_state_string (p), "Exited", "State is now 'Exited'");
        is (subprocess_exit_string (p), "Exited with non-zero status",
                "State is now 'Exited with non-zero status'");
        ok (subprocess_exit_code (p) == 1, "Exit code is 1.");
        subprocess_destroy (p);
        q = NULL;
    }

    diag ("Test signaled program");

    /* Test signaled program */
    p = subprocess_manager_run (sm, 2, args4, NULL);
    ok (p != NULL, "subprocess_manager_run: %s", strerror (errno));
    if (p) {
        ok (subprocess_pid (p) != (pid_t) -1, "process has valid pid");

        ok (subprocess_kill (p, SIGKILL) >= 0, "subprocess_kill");

        q = subprocess_manager_wait (sm);
        ok (p == q, "subprocess_manager_wait returns correct process");
        is (subprocess_state_string (p), "Exited", "State is now 'Exited'");
        is (subprocess_exit_string (p), "Killed", "Exit string is 'Killed'");
        ok (subprocess_signaled (p) == 9, "Killed by signal 9.");
        ok (subprocess_exit_status (p) == 0x9, "Exit status is 0x9 (Killed)");
        ok (subprocess_exit_code (p) == 137, "Exit code is 137 (128+9)");
        subprocess_destroy (p);
    }

    q = NULL;

    diag ("Test fork/exec interface");
    /* Test separate fork/exec interface */
    p = subprocess_create (sm);
    ok (p != NULL, "subprocess_create works");
    ok (subprocess_pid (p) == (pid_t) -1, "Initial pid value is -1");
    ok (subprocess_fork (p) == -1, "fork on unitialized subprocess should fail");
    ok (subprocess_kill (p, 1) == -1, "kill on unitialized subprocess should fail");
    is (subprocess_state_string (p), "Pending",
        "initial subprocess state is 'Pending'");

    ok (subprocess_argv_append (p, "true") >= 0, "set argv");
    ok (subprocess_setenv (p, "PATH", getenv ("PATH"), 1) >= 0, "set dnv");

    ok (subprocess_fork (p) == 0, "subprocess_fork");
    is (subprocess_state_string (p), "Waiting", "subprocess is Waiting");
    ok (subprocess_pid (p) > 0, "subprocess_pid() is valid");

    ok (subprocess_exec (p) == 0, "subprocess_run");
    is (subprocess_state_string (p), "Running", "subprocess is Running");
    q = subprocess_manager_wait (sm);
    ok (q != NULL, "subprocess_manager_wait");
    ok (q == p, "got correct child after wait");

    ok (subprocess_exit_code (p) == 0, "Child exited normally");

    subprocess_destroy (p);
    q = NULL;

    diag ("Test exec failure");
    /* Test exec failure */
    p = subprocess_create (sm);
    ok (p != NULL, "subprocess create");
    ok (subprocess_argv_append (p, "/unlikely/program") >= 0, "set argv");
    ok (subprocess_setenv (p, "PATH", getenv ("PATH"), 1) >= 0, "setnv");

    ok (subprocess_fork (p) == 0, "subprocess_fork");
    rc = subprocess_exec (p);
    ok (rc < 0, "subprocess_exec should fail");
    ok (errno == ENOENT, "errno should be ENOENT");
    is (subprocess_state_string (p), "Exec Failure", "State is Exec Failed");
    is (subprocess_exit_string (p), "Exec Failure", "Exit state is Exec Failed");
    subprocess_destroy (p);

    diag ("Test set working directory");
    /* Test set working directory */
    p = subprocess_create (sm);
    ok (p != NULL, "subprocess create");
    ok (subprocess_get_cwd (p) == NULL, "CWD is not set");
    ok (subprocess_set_cwd (p, "/tmp") >= 0, "Set CWD to /tmp");
    is (subprocess_get_cwd (p), "/tmp", "CWD is now /tmp");
    ok (subprocess_setenv (p, "PATH", getenv ("PATH"), 1) >= 0, "set PATH");
    ok (subprocess_set_command (p, "test `pwd` = '/tmp'" ) >= 0, "Set args");
    ok (subprocess_run (p) >= 0, "subprocess_run");
    is (subprocess_state_string (p), "Running", "subprocess now running");
    q = subprocess_manager_wait (sm);
    ok (q != NULL, "subprocess_manager_wait: %s", strerror (errno));
    ok (q == p, "subprocess_manager_wait() got expected subprocess");
    ok (subprocess_exited (p), "subprocess exited");
    ok (!subprocess_signaled (p), "subprocess didn't die from signal");
    ok (subprocess_exit_code (p) == 0, "subprocess successfully run in /tmp");
    subprocess_destroy (p);

    diag ("Test subprocess_reap interface");
    /* Test subprocess_reap */
    p = subprocess_create (sm);
    q = subprocess_create (sm);

    ok (subprocess_argv_append (p, "/bin/true") >= 0,
        "set argv for first subprocess");
    ok (subprocess_argv_append (q, "/bin/true") >= 0,
        "set argv for second subprocess");
    ok (subprocess_run (p) >= 0, "run process 1");
    ok (subprocess_run (q) >= 0, "run process 2");

    ok (subprocess_reap (q) >= 0, "reap process 2");
    ok (subprocess_exited (q), "process 2 is now exited");
    ok (subprocess_exit_code (q) == 0, "process 2 exited with code 0");

    ok (subprocess_reap (p) >= 0, "reap process 1");
    ok (subprocess_exited (p), "process 1 is now exited");
    ok (subprocess_exit_code (p) == 0, "process 1 exited with code 0");

    subprocess_destroy (p);
    subprocess_destroy (q);

    diag ("Test subprocess I/O");
    /* Test subprocess output */
    p = subprocess_create (sm);
    ok (p != NULL, "subprocess_create");
    ok (subprocess_argv_append (p, "/bin/echo") >= 0,  "subprocess_argv_append");
    ok (subprocess_argv_append (p, "Hello, 123") >= 0, "subprocess_argv_append");

    buf = NULL;
    subprocess_set_context (p, "io", (void *) &buf);
    ok (subprocess_get_context (p, "io") == (void *) &buf, "able to set subprocess context");

    ok (subprocess_set_io_callback (p, testio_cb) >= 0, "set io callback");

    ok (subprocess_run (p) >= 0, "run process with IO");

    ok (subprocess_reap (p) >= 0, "reap process");
    ok (subprocess_flush_io (p) >=0, "flush io");

    ok (subprocess_exited (p) >= 0, "process is now exited");
    ok (subprocess_exit_code (p) == 0, "process exited normally");

    ok (buf != NULL, "io buffer is allocated");
    if (buf) {
        ok (strcmp (buf, "Hello, 123\n") == 0, "io buffer is correct");
        free (buf);
    }
    subprocess_destroy (p);


    /* Test subprocess input */
    diag ("test subprocess stdin");
    p = subprocess_create (sm);
    ok (p != NULL, "subprocess_create");
    ok (subprocess_argv_append (p, "/bin/cat") >= 0,  "subprocess_argv_append");

    buf = NULL;
    subprocess_set_context (p, "io", (void *) &buf);
    ok (subprocess_get_context (p, "io") == (void *) &buf, "able to set subprocess context");

    ok (subprocess_set_io_callback (p, testio_cb) >= 0, "set io callback");

    ok (subprocess_run (p) >= 0, "run process with IO");

    ok (subprocess_write (p, "Hello\n", 7, true) >= 0, "write to subprocess");
    ok (subprocess_reap (p) >= 0, "reap process");
    ok (subprocess_flush_io (p) >= 0, "manually flush io");
    ok (subprocess_io_complete (p) == 1, "io is now complete");

    ok (subprocess_exited (p) >= 0, "process is now exited");
    ok (subprocess_exit_code (p) == 0, "process exited normally");

    ok (buf != NULL, "io buffer is allocated");
    if (buf) {
        ok (strcmp (buf, "Hello\n") == 0, "io buffer is correct");
        free (buf);
    }
    subprocess_destroy (p);
    subprocess_manager_destroy (sm);

    end_fdcount = fdcount ();
    diag ("final fd count %d", end_fdcount);
    ok (start_fdcount == end_fdcount,
        "no file descriptors were leaked");

    done_testing ();
}

static int testio_cb (struct subprocess *p, const char *json_str)
{
    char **bufp = subprocess_get_context (p, "io");
    bool eof;
    if (*bufp == NULL)
        zio_json_decode (json_str, (void **) bufp, &eof);
    return 0;
}


static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}


/*
 * vi: ts=4 sw=4 expandtab
 */
