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

#include <unistd.h>
#include <signal.h>
#include <spawn.h>

#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fdwalk.h"

#include "subprocess_private.h"
#include "command_private.h"

struct spawn_close_arg {
    posix_spawn_file_actions_t *fa;
    struct idset *childfds;
};

static void spawn_closefd (void *arg, int fd)
{
    struct spawn_close_arg *sc = arg;
    if (idset_test (sc->childfds, fd))
        return;
    posix_spawn_file_actions_addclose (sc->fa, fd);
}

/*  Setup posix_spawn_file_actions_t for this subprocess.
 *  - dup stdin/out/err fds onto standard file descriptor numbers
 *  - set up other file descriptors to close in the child
 */
static int spawn_setup_fds (flux_subprocess_t *p,
                            posix_spawn_file_actions_t *fa)
{
    int rc = -1;
    struct subprocess_channel *c;
    struct spawn_close_arg sc;

    sc.fa = fa;
    if (!(sc.childfds = subprocess_childfds (p)))
        return -1;

    if (!(p->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)) {
        if ((c = zhash_lookup (p->channels, "stdin"))) {
            if (posix_spawn_file_actions_adddup2 (fa,
                                                  c->child_fd,
                                                  STDIN_FILENO) < 0)
                goto out;
        }
        if ((c = zhash_lookup (p->channels, "stdout"))) {
            if (posix_spawn_file_actions_adddup2 (fa,
                                                  c->child_fd,
                                                  STDOUT_FILENO) < 0)
                goto out;
        }
        else if (posix_spawn_file_actions_addclose (fa, STDOUT_FILENO) < 0)
                goto out;

        if ((c = zhash_lookup (p->channels, "stderr"))) {
            if (posix_spawn_file_actions_adddup2 (fa,
                                                  c->child_fd,
                                                  STDERR_FILENO) < 0)
                goto out;
        }
        else if (posix_spawn_file_actions_addclose (fa, STDERR_FILENO) < 0)
                goto out;
    }

    if (fdwalk (spawn_closefd, (void *) &sc) < 0)
        goto out;

    rc = 0;
out:
    idset_destroy (sc.childfds);
    return rc;
}

/*  Reset (most) signals to default mask and handlers in child.
 */
static int setup_signals (posix_spawnattr_t *attr)
{
    sigset_t mask;

    if (sigemptyset (&mask) < 0
        || posix_spawnattr_setsigmask (attr, &mask) < 0)
        return -1;

    /*  Iterate list of signals to reset.
     *
     *  Note: It has been experimentally determined that setting
     *  unblockable signals like SIGKILL and SIGSTOP, as well as high
     *  signal numbers (> 64)  in the sigdefault mask cause spawn
     *  failures in the child process (exit code 127). Therefore, we
     *  have to be more targeted than just using sigfillset(3).
     */
    for (int i = 1; i < SIGSYS; i++) {
        if (i != SIGKILL && i != SIGSTOP)
            if (sigaddset (&mask, i) < 0)
                return -1;
    }
    return posix_spawnattr_setsigdefault (attr, &mask);
}

/*  Create a child process using posix_spawnp(3).
 */
int create_process_spawn (flux_subprocess_t *p)
{
    int rc = -1;
    int retval;
    int saved_errno;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attr;
    short flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
    char **env = cmd_env_expand (p->cmd);
    char **argv = cmd_argv_expand (p->cmd);

    posix_spawnattr_init (&attr);
    posix_spawn_file_actions_init (&file_actions);

    if (setup_signals (&attr) < 0)
        goto out;

    /*  If setpgrp(2) is desired for the child process, then add this
     *  flag to the spawnattr flags.
     */
    if (!(p->flags & FLUX_SUBPROCESS_FLAGS_NO_SETPGRP))
        flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setflags (&attr, flags);

    /*  Setup file descriptors in file_actions
     */
    spawn_setup_fds (p, &file_actions);

    /*  Attempt to spawn a new child process */
    retval = posix_spawnp (&p->pid, argv[0], &file_actions, &attr, argv, env);
    if (retval != 0) {
        errno = retval;
        goto out;
    }
    p->pid_set = true;
    rc = 0;
out:
    saved_errno = errno;
    posix_spawnattr_destroy (&attr);
    posix_spawn_file_actions_destroy (&file_actions);

    free (env);
    free (argv);

    errno = saved_errno;
    return rc;
}

/*  vi: ts=4 sw=4 expandtab
 */
