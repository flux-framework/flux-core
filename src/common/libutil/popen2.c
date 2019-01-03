/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "popen2.h"
#include "fdwalk.h"
#include "fdutils.h"

#define PXOPEN_CHILD_MAGIC 0xc00ceeee

struct popen2_child {
    int magic;
    int fd[2];
    int ctl[2];
    pid_t pid;
};

enum {
    SP_PARENT = 0,
    SP_CHILD = 1,
};

int popen2_get_fd (struct popen2_child *p)
{
    if (!p || p->magic != PXOPEN_CHILD_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    return p->fd[SP_PARENT];
}

static void popen2_child_close_fd (void *arg, int fd)
{
    struct popen2_child *p = arg;
    if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO
                                                  && fd != p->ctl[SP_CHILD])
        (void)close (fd);
}

static void child (struct popen2_child *p, const char *path, char *const argv[])
{
    int saved_errno;

    (void)close (STDIN_FILENO);
    (void)close (STDOUT_FILENO);
    if (    dup2 (p->fd[SP_CHILD], STDIN_FILENO) < 0
         || dup2 (p->fd[SP_CHILD], STDOUT_FILENO) < 0) {
        saved_errno = errno;
        goto error;
    }
    (void)close (p->fd[SP_CHILD]);

    if (fdwalk (popen2_child_close_fd, p)) {
        saved_errno = errno;
        goto error;
    }
    execvp (path, argv);
    saved_errno = errno;
error:
    if (write (p->ctl[SP_CHILD], &saved_errno, sizeof (saved_errno)) < 0)
        fprintf (stderr, "child: write to ctl failed: %s\n", strerror (errno));
    (void) close (p->ctl[SP_CHILD]);
    exit (0);
}

struct popen2_child *popen2 (const char *path, char *const argv[])
{
    struct popen2_child *p = NULL;
    int n, saved_errno;

    if (!(p = malloc (sizeof (*p)))) {
        saved_errno = ENOMEM;
        goto error;
    }
    memset (p, 0, sizeof (*p));
    p->magic = PXOPEN_CHILD_MAGIC;
    p->fd[SP_CHILD] = -1;
    p->fd[SP_PARENT] = -1;
    p->ctl[SP_CHILD] = -1;
    p->ctl[SP_PARENT] = -1;
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, p->fd) < 0) {
        saved_errno = errno;
        goto error;
    }
    if (fd_set_cloexec (p->fd[SP_PARENT]) < 0) {
        saved_errno = errno;
        goto error;
    }
    if (pipe2 (p->ctl, O_CLOEXEC) < 0) {
        saved_errno = errno;
        goto error;
    }
    signal(SIGPIPE, SIG_IGN);
    switch ((p->pid = fork ())) {
        case -1:    /* fork error */
            saved_errno = errno;
            goto error;
        case 0:     /* child */
            child (p, path, argv);
            /*NOTREACHED*/
        default:    /* parent */
            break;
    }
    (void)close (p->fd[SP_CHILD]);
    (void)close (p->ctl[SP_CHILD]);
    p->fd[SP_CHILD] = -1;
    p->ctl[SP_CHILD] = -1;

    /* Handshake on ctl pipe to make sure exec worked.
     * If exec successful, child end will be closed with no data.
     * If exec failed, errno will be returned on pipe.
     */
    n = read (p->ctl[SP_PARENT], &saved_errno, sizeof (saved_errno));
    if (n == sizeof (saved_errno))
        goto error;
    if (n < 0) {
        saved_errno = errno;
        goto error;
    }
    if (n != 0) {
        saved_errno = EPROTO;
        goto error;
    }
    (void)close (p->ctl[SP_PARENT]);
    p->ctl[SP_PARENT] = -1;

    return p;
error:
    pclose2 (p);
    errno = saved_errno;
    return NULL;
}

int pclose2 (struct popen2_child *p)
{
    int status, saved_errno = 0;
    int rc = 0;

    if (p) {
        if (p->magic != PXOPEN_CHILD_MAGIC) {
            saved_errno = EINVAL;
            rc = -1;
            goto done;
        }
        if (p->fd[SP_PARENT] && shutdown (p->fd[SP_PARENT], SHUT_WR) < 0) {
            saved_errno = errno;
            rc = -1;
        }
        if (p->pid != 0) {
            if (waitpid (p->pid, &status, 0) < 0) {
                saved_errno = errno;
                rc = -1;
            } else {
                if (!WIFEXITED (status) || WEXITSTATUS (status) != 0) {
                    saved_errno = EIO;
                    rc = -1;
                }
            }
        }
        if ((p->fd[SP_PARENT] >= 0 && close (p->fd[SP_PARENT]) < 0)
         || (p->fd[SP_CHILD] >= 0 && close (p->fd[SP_CHILD]) < 0)
         || (p->ctl[SP_PARENT] >= 0 && close (p->ctl[SP_PARENT]) < 0)
         || (p->ctl[SP_CHILD] >= 0 && close (p->ctl[SP_CHILD]) < 0)) {
            saved_errno = errno;
            rc = -1;
        }
        p->magic = ~PXOPEN_CHILD_MAGIC;
        free (p);
    }
done:
    if (rc == -1)
        errno = saved_errno;
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
