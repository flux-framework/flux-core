
/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <sys/wait.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"

#include "sigchld.h"

int multi_errors;
int multi_counter;
void multi_child_exit_cb (pid_t pid, int status, void *arg)
{
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
        multi_errors++;
    sigchld_unregister (pid);
    multi_counter++;
}

void test_multi_child_exit (flux_reactor_t *r)
{
    pid_t pids[64];
    int count;

    ok (sigchld_initialize (r) == 0,
        "sigchld_initialize worked");

    for (int i = 0; i < ARRAY_SIZE (pids); i++) {
        if ((pids[i] = fork ()) < 0)
            BAIL_OUT ("fork failed: %s", strerror (errno));
        if (pids[i] == 0) // child
            _exit (0);
        if (sigchld_register (r, pids[i], multi_child_exit_cb, &count) < 0)
            BAIL_OUT ("could not register sigchld handler");
    }
    multi_errors = 0;
    multi_counter = 0;

    diag ("registered %d handlers", ARRAY_SIZE (pids));

    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran to completion with no error");

    ok (multi_errors == 0,
        "there were no errors");
    ok (multi_counter == ARRAY_SIZE (pids),
        "the callback ran the right number of times");

    sigchld_finalize ();
}

void child_exit_cb (pid_t pid, int status, void *arg)
{
    pid_t rpid = *(pid_t *)arg;
    ok (pid == rpid,
        "callback pid and argument match as expected");
    ok (WIFEXITED (status) && WEXITSTATUS (status) == 1,
        "child exit 1");

    // ensure these are safe within callback
    sigchld_unregister (pid);
    sigchld_finalize ();
}

void test_child_exit (flux_reactor_t *r)
{
    pid_t pid;

    ok (sigchld_initialize (r) == 0,
        "sigchld_initialize worked");
    ok (sigchld_initialize (r) == 0,
        "sigchld_initialize worked one more time");
    sigchld_finalize ();
    diag ("dropped extra sigchld context reference");

    if ((pid = fork ()) < 0)
        BAIL_OUT ("fork failed: %s", strerror (errno));
    if (pid == 0) // child
        _exit (1);
    diag ("forked child %d", pid);
    ok (sigchld_register (r, pid, child_exit_cb, &pid) == 0,
        "sigchld_register worked");

    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran to completion with no error");

    // extra calls should be no-ops
    sigchld_unregister (pid);
    sigchld_finalize ();
}

int main (int argc, char **argv)
{
    flux_reactor_t *r;

    plan (NO_PLAN);

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("could not create reactor");

    test_child_exit (r);
    test_multi_child_exit (r);

    flux_reactor_destroy (r);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
