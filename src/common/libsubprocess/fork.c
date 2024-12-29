/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/llog.h"

#include "subprocess_private.h"
#include "command_private.h"

extern char **environ;

static int sigmask_unblock_all (void)
{
    sigset_t mask;
    sigemptyset (&mask);
    return sigprocmask (SIG_SETMASK, &mask, NULL);
}

static void close_parent_fds (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    c = zhash_first (p->channels);
    while (c) {
        if (c->parent_fd >= 0) {
            close (c->parent_fd);
            c->parent_fd = -1;
        }
        c = zhash_next (p->channels);
    }
}

static void closefd_child (void *arg, int fd)
{
    struct idset *ids = arg;
    if (idset_test (ids, fd))
        return;
    close (fd);
}

/*  Signal parent that child is ready for exec(2) and wait for parent's
 *   signal to proceed. This is done by writing 1 byte to child side of
 *   socketpair, and waiting for parent to write one byte back.
 *
 * Call fprintf instead of llog_error(), errors in child should
 *  go to parent error streams.
 */
static int local_child_ready (flux_subprocess_t *p)
{
    int n;
    int fd = p->sync_fds[1];
    char c = 0;

    if (write (fd, &c, sizeof (c)) != 1) {
        fprintf (stderr, "local_child_ready: write: %s\n", strerror (errno));
        return -1;
    }
    if ((n = read (fd, &c, sizeof (c))) != 1) {
        fprintf (stderr, "local_child_ready: read (fd=%d): rc=%d: %s\n",
                 fd, n, strerror (errno));
        return -1;
    }
    return 0;
}

static void local_child_report_exec_failed_errno (flux_subprocess_t *p, int e)
{
    int fd = p->sync_fds[1];
    /* Call fprintf instead of llog_error(), errors in child
     * should go to parent error streams. */
    if (write (fd, &e, sizeof (e)) != sizeof (e))
        fprintf (stderr, "local_child_report_exec_failed_errno: %s\n",
                 strerror (errno));
}

#if CODE_COVERAGE_ENABLED
void __gcov_dump (void);
void __gcov_reset (void);
#endif
static int local_child (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    int errnum;
    char **argv;
    const char *cwd;
    struct idset *ids;

    /* Throughout this function use _exit() instead of exit(), to
     * avoid calling any atexit() routines of parent.
     *
     * Call fprintf instead of llog_error(), errors in child
     * should go to parent error streams.
     */

    if (sigmask_unblock_all () < 0)
        fprintf (stderr, "sigprocmask: %s\n", strerror (errno));

    close_parent_fds (p);

    if (!(p->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)) {
        if ((c = zhash_lookup (p->channels, "stdin"))) {
            if (dup2 (c->child_fd, STDIN_FILENO) < 0) {
                fprintf (stderr, "dup2: %s\n", strerror (errno));
                _exit (1);
            }
        }

        if ((c = zhash_lookup (p->channels, "stdout"))) {
            if (dup2 (c->child_fd, STDOUT_FILENO) < 0) {
                fprintf (stderr, "dup2: %s\n", strerror (errno));
                _exit (1);
            }
        }
        else
            close (STDOUT_FILENO);

        if ((c = zhash_lookup (p->channels, "stderr"))) {
            if (dup2 (c->child_fd, STDERR_FILENO) < 0) {
                fprintf (stderr, "dup2: %s\n", strerror (errno));
                _exit (1);
            }
        }
        else
            close (STDERR_FILENO);
    }

    // Change working directory
    if ((cwd = flux_cmd_getcwd (p->cmd)) && chdir (cwd) < 0) {
        fprintf (stderr,
                 "Could not change dir to %s: %s. Going to /tmp instead\n",
                 cwd, strerror (errno));
        if (chdir ("/tmp") < 0)
            _exit (1);
    }

    // Send ready to parent
    if (local_child_ready (p) < 0)
        _exit (1);

    // Close fds
    if (!(ids = subprocess_childfds (p))
        || fdwalk (closefd_child, (void *) ids) < 0) {
        fprintf (stderr, "Failed closing all fds: %s", strerror (errno));
        _exit (1);
    }
    idset_destroy (ids);

    if (p->hooks.pre_exec) {
        fd_set_nonblocking (STDERR_FILENO);
        fd_set_nonblocking (STDOUT_FILENO);
        p->in_hook = true;
        (*p->hooks.pre_exec) (p, p->hooks.pre_exec_arg);
        p->in_hook = false;
        fd_set_blocking (STDERR_FILENO);
        fd_set_blocking (STDOUT_FILENO);
    }

    if (!(p->flags & FLUX_SUBPROCESS_FLAGS_NO_SETPGRP)
        && getpgrp () != getpid ()) {
        if (setpgrp () < 0) {
            fprintf (stderr, "setpgrp: %s\n", strerror (errno));
            _exit (1);
        }
    }

    environ = cmd_env_expand (p->cmd);
    argv = cmd_argv_expand (p->cmd);
    if (!environ || !argv) {
        fprintf (stderr, "out of memory\n");
        _exit (1);
    }
#if CODE_COVERAGE_ENABLED
    __gcov_dump ();
    __gcov_reset ();
#endif
    execvp (argv[0], argv);

    errnum = errno;
    /*
     * NB: close stdout and stderr here to avoid flushing buffers at exit.
     *  This can cause duplicate output if parent was running in fully
     *  buffered mode, and there was buffered output.
     */
    close (STDOUT_FILENO);
    local_child_report_exec_failed_errno (p, errnum);
    close (STDERR_FILENO);
#if CODE_COVERAGE_ENABLED
    __gcov_dump ();
    __gcov_reset ();
#endif
    /* exit code doesn't matter, can't be returned to user */
    _exit (1);
}

/*  Wait for child to indicate it is ready for exec(2) by doing a blocking
 *   read() of one byte on parent side of sync_fds.
 */
static int subprocess_parent_wait_on_child (flux_subprocess_t *p)
{
    char c;

    if (read (p->sync_fds[0], &c, sizeof (c)) != 1) {
        llog_debug (p,
                    "subprocess_parent_wait_on_child: read: %s",
                    strerror (errno));
        return -1;
    }
    return 0;
}

/*  Signal child to proceed with exec(2) and read any error from exec
 *   back on sync_fds.  Return < 0 on failure to signal, or > 0 errnum if
 *   an exec error was returned from child.
 */
static int local_release_child (flux_subprocess_t *p)
{
    int fd = p->sync_fds[0];
    char c = 0;
    int e = 0;
    ssize_t n;

    if (write (fd, &c, sizeof (c)) != 1)
        return -1;
    if ((n = read (fd, &e, sizeof (e))) < 0)
        return -1;
    else if (n == sizeof (int)) {
        // exec error received
        return e;
    }
    /* else n == 0, child exec'ed and closed sync_fds[1] */

    /* no longer need this fd */
    close (p->sync_fds[0]);
    p->sync_fds[0] = -1;
    return 0;
}

static int local_exec (flux_subprocess_t *p)
{
    int ret;
    /* N.B. We don't set p->failed_errno here, if locally launched via
     * flux_local_exec(), will return -1 and errno to caller.  If
     * called via server, p->failed_errno will be set by remote
     * handler. */
    if ((ret = local_release_child (p)) != 0) {
        /*
         *  Reap child immediately. Expectation from caller is that
         *   failure to exec will not require subsequent reaping of
         *   child.
         */
        int status;
        pid_t pid;
        if ((pid = waitpid (p->pid, &status, 0)) <= 0)
            return -1;
        p->status = status;

        errno = ret;
        return -1;
    }
    return 0;
}

int create_process_fork (flux_subprocess_t *p)
{
    if ((p->pid = fork ()) < 0)
        return -1;

    if (p->pid == 0)
        local_child (p); /* No return */

    p->pid_set = true;

    /*  close child end of the sync_fd */
    close (p->sync_fds[1]);
    p->sync_fds[1] = -1;

    if (subprocess_parent_wait_on_child (p) < 0)
        return -1;

    return local_exec (p);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
