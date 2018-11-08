/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "local.h"
#include "util.h"

static void local_channel_flush (struct subprocess_channel *c)
{
    /* This is a full channel with read and write, a close on the
     * write side needs to "generate" an EOF on the read side
     */
    if (!(c->flags & CHANNEL_READ))
        return;

    if (!c->eof_sent_to_caller && c->output_f) {
        flux_buffer_t *fb;
        int len;

        if (!(fb = flux_buffer_read_watcher_get_buffer (c->buffer_read_w))) {
            flux_log_error (c->p->h, "flux_buffer_read_watcher_get_buffer");
            return;
        }

        while ((len = flux_buffer_bytes (fb)) > 0)
            c->output_f (c->p, c->name);

        /* eof call */
        c->output_f (c->p, c->name);

        c->eof_sent_to_caller = true;
        c->p->channels_eof_sent++;
        flux_watcher_stop (c->buffer_read_w);

        if (c->p->state == FLUX_SUBPROCESS_EXITED && c->eof_sent_to_caller)
            subprocess_check_completed (c->p);
    }
}

static void local_in_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    struct subprocess_channel *c = (struct subprocess_channel *)arg;
    int err = 0;

    if (flux_buffer_write_watcher_is_closed (w, &err) == 1) {
        if (err)
            log_msg ("flux_buffer_write_watcher close error: %s",
                     strerror (err));
        else
            c->parent_fd = -1;  /* closed by reactor */
        flux_watcher_stop (w);  /* c->buffer_write_w */
        local_channel_flush (c);
    }
    else
        flux_log_error (c->p->h, "flux_buffer_write_watcher: stream %s: %d:",
                        c->name, revents);
}

static void local_output (struct subprocess_channel *c,
                          flux_watcher_t *w, int revents,
                          flux_subprocess_output_f output_cb)
{
    bool eof_set = false;

    if (revents & FLUX_POLLIN) {
        flux_buffer_t *fb;
        if (!c->eof_sent_to_caller) {

            if (!(fb = flux_buffer_read_watcher_get_buffer (w))) {
                flux_log_error (c->p->h, "flux_buffer_read_watcher_get_buffer");
                return;
            }

            if (!flux_buffer_bytes (fb)) {
                c->eof_sent_to_caller = true;
                eof_set = true;
                c->p->channels_eof_sent++;
            }
        }

        output_cb (c->p, c->name);

        if (c->p->state == FLUX_SUBPROCESS_EXITED && !c->eof_sent_to_caller) {

            if (!(fb = flux_buffer_read_watcher_get_buffer (w))) {
                flux_log_error (c->p->h, "flux_buffer_read_watcher_get_buffer");
                return;
            }

            if (!flux_buffer_bytes (fb)) {

                output_cb (c->p, c->name);

                c->eof_sent_to_caller = true;
                eof_set = true;
                c->p->channels_eof_sent++;
            }
        }
    }
    else
        flux_log_error (c->p->h, "flux_buffer_read_watcher on %s: 0x%X:",
                        c->name, revents);

    if (eof_set) {
        flux_watcher_stop (w);

        /* if the read pipe is ended, then we can go ahead and close
         * the write side as well.  Note that there is no need to
         * "flush" the write buffer.  If we've received the EOF on the
         * read side, no more writes matter.
         */
        if (c->flags & CHANNEL_WRITE) {
            flux_watcher_stop (c->buffer_write_w);
            c->closed = true;
        }
    }

    if (c->p->state == FLUX_SUBPROCESS_EXITED && c->eof_sent_to_caller)
        subprocess_check_completed (c->p);
}

static void local_out_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    struct subprocess_channel *c = (struct subprocess_channel *)arg;
    local_output (c, w, revents, c->p->ops.on_channel_out);
}

static void local_stdout_cb (flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg)
{
    struct subprocess_channel *c = (struct subprocess_channel *)arg;
    local_output (c, w, revents, c->p->ops.on_stdout);
}

static void local_stderr_cb (flux_reactor_t *r, flux_watcher_t *w,
                               int revents, void *arg)
{
    struct subprocess_channel *c = (struct subprocess_channel *)arg;
    local_output (c, w, revents, c->p->ops.on_stderr);
}

static int channel_local_setup (flux_subprocess_t *p,
                                flux_subprocess_output_f output_f,
                                flux_watcher_f in_cb,
                                flux_watcher_f out_cb,
                                const char *name,
                                int channel_flags,
                                int buffer_size)
{
    struct subprocess_channel *c = NULL;
    int fds[2] = { -1, -1 };
    char *e = NULL;
    int save_errno;
    int fd_flags;

    if (!(c = channel_create (p, output_f, name, channel_flags))) {
        flux_log_error (p->h, "calloc");
        goto error;
    }

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fds) < 0) {
        flux_log_error (p->h, "socketpair");
        goto error;
    }

    c->parent_fd = fds[0];
    c->child_fd = fds[1];

    /* set fds[] to -1, on error is now subprocess_free()'s
     * responsibility
     */
    fds[0] = -1;
    fds[1] = -1;

    if ((channel_flags & CHANNEL_WRITE) && in_cb) {
        c->buffer_write_w = flux_buffer_write_watcher_create (p->reactor,
                                                              c->parent_fd,
                                                              buffer_size,
                                                              in_cb,
                                                              0,
                                                              c);
        if (!c->buffer_write_w) {
            flux_log_error (p->h, "flux_buffer_write_watcher_create");
            goto error;
        }
    }

    if ((fd_flags = fcntl (c->parent_fd, F_GETFL)) < 0
                 || fcntl (c->parent_fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
        flux_log_error (p->h, "fcntl");
        goto error;
    }

    if ((channel_flags & CHANNEL_READ) && out_cb) {
        c->buffer_read_w = flux_buffer_read_watcher_create (p->reactor,
                                                            c->parent_fd,
                                                            buffer_size,
                                                            out_cb,
                                                            0,
                                                            c);
        if (!c->buffer_read_w) {
            flux_log_error (p->h, "flux_buffer_read_watcher_create");
            goto error;
        }

        p->channels_eof_expected++;
    }

    if (channel_flags & CHANNEL_FD) {
        if (asprintf (&e, "%s", name) < 0) {
            flux_log_error (p->h, "asprintf");
            goto error;
        }

        /* set overwrite flag, if caller recursively launches
         * another subprocess */
        if (flux_cmd_setenvf (p->cmd,
                              1,
                              e,
                              "%d",
                              c->child_fd) < 0) {
            flux_log_error (p->h, "flux_cmd_setenvf");
            goto error;
        }
    }

    if (zhash_insert (p->channels, name, c) < 0) {
        flux_log_error (p->h, "zhash_insert");
        goto error;
    }
    if (!zhash_freefn (p->channels, name, channel_destroy)) {
        flux_log_error (p->h, "zhash_freefn");
        goto error;
    }

    /* now error is in subprocess_free()'s responsibility
     */
    c = NULL;

    free (e);
    return 0;

error:
    save_errno = errno;
    close_pair_fds (fds);
    channel_destroy (c);
    free (e);
    errno = save_errno;
    return -1;
}

static int local_setup_stdio (flux_subprocess_t *p)
{
    int buffer_size;

    if (p->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)
        return 0;

    /* stdio is identical to channels, except they are limited to read
     * and/or write, and the buffer's automatically get a NUL char
     * appended on reads */

    if ((buffer_size = cmd_option_bufsize (p, "STDIN")) < 0)
        return -1;

    if (channel_local_setup (p,
                             NULL,
                             local_in_cb,
                             NULL,
                             "STDIN",
                             CHANNEL_WRITE,
                             buffer_size) < 0)
        return -1;

    if (p->ops.on_stdout) {
        if ((buffer_size = cmd_option_bufsize (p, "STDOUT")) < 0)
            return -1;

        if (channel_local_setup (p,
                                 p->ops.on_stdout,
                                 NULL,
                                 local_stdout_cb,
                                 "STDOUT",
                                 CHANNEL_READ,
                                 buffer_size) < 0)
            return -1;
    }

    if (p->ops.on_stderr) {
        if ((buffer_size = cmd_option_bufsize (p, "STDERR")) < 0)
            return -1;

        if (channel_local_setup (p,
                                 p->ops.on_stderr,
                                 NULL,
                                 local_stderr_cb,
                                 "STDERR",
                                 CHANNEL_READ,
                                 buffer_size) < 0)
            return -1;
    }

    return 0;
}

static int local_setup_channels (flux_subprocess_t *p)
{
    zlist_t *channels;
    const char *name;
    int channel_flags = CHANNEL_READ | CHANNEL_WRITE | CHANNEL_FD;
    int len;

    if (!(channels = flux_cmd_channel_list (p->cmd))) {
        flux_log_error (p->h, "flux_cmd_channel_list");
        return -1;
    }

    if (!(len = zlist_size (channels)))
        return 0;

    if (!p->ops.on_channel_out)
        channel_flags &= ~CHANNEL_READ;

    name = zlist_first (channels);
    while (name) {
        int buffer_size;

        if ((buffer_size = cmd_option_bufsize (p, name)) < 0)
            return -1;

        if (channel_local_setup (p,
                                 p->ops.on_channel_out,
                                 local_in_cb,
                                 p->ops.on_channel_out ? local_out_cb : NULL,
                                 name,
                                 channel_flags,
                                 buffer_size) < 0)
            return -1;
        name = zlist_next (channels);
    }

    return 0;
}

static int sigmask_unblock_all (void)
{
    sigset_t mask;
    sigemptyset (&mask);
    return sigprocmask (SIG_SETMASK, &mask, NULL);
}

static void close_fds (flux_subprocess_t *p, bool parent)
{
    struct subprocess_channel *c;
    int f = parent ? 0 : 1;

    close (p->sync_fds[f]);
    p->sync_fds[f] = -1;

    /* note, it is safe to iterate via zhash, child & parent will have
     * different copies of zhash */
    c = zhash_first (p->channels);
    while (c) {
        if (parent && c->parent_fd != -1) {
            close (c->parent_fd);
            c->parent_fd = -1;
        }
        else if (!parent && c->child_fd != -1) {
            close (c->child_fd);
            c->child_fd = -1;
        }
        c = zhash_next (p->channels);
    }
}

static void close_parent_fds (flux_subprocess_t *p)
{
    close_fds (p, true);
}

static void close_child_fds (flux_subprocess_t *p)
{
    close_fds (p, false);
}

static void closefd_child (void *arg, int fd)
{
    flux_subprocess_t *p = arg;
    struct subprocess_channel *c;
    if (fd < 3 || fd == p->sync_fds[1])
        return;
    c = zhash_first (p->channels);
    while (c) {
        if (c->child_fd == fd) {
            int flags = fcntl (fd, F_GETFD, 0);
            if (flags >= 0)
                (void) fcntl (fd, F_SETFD, flags & ~FD_CLOEXEC);
            return;
        }
        c = zhash_next (p->channels);
    }
    close (fd);
}

/*  Signal parent that child is ready for exec(2) and wait for parent's
 *   signal to proceed. This is done by writing 1 byte to child side of
 *   socketpair, and waiting for parent to write one byte back.
 *
 */
static int local_child_ready (flux_subprocess_t *p)
{
    int n;
    int fd = p->sync_fds[1];
    char c = 0;

    if (write (fd, &c, sizeof (c)) != 1) {
        flux_log_error (p->h, "local_child_ready: write");
        return -1;
    }
    if ((n = read (fd, &c, sizeof (c))) != 1) {
        flux_log_error (p->h, "local_child_ready: read (fd=%d): rc=%d", fd, n);
        return -1;
    }
    return 0;
}

static void local_child_report_exec_failed_errno (flux_subprocess_t *p, int e)
{
    int fd = p->sync_fds[1];
    if (write (fd, &e, sizeof (e)) != sizeof (e))
        flux_log_error (p->h, "local_child_report_exec_failed_errno");
}

static int local_child (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    int errnum;
    char **argv;
    const char *cwd;

    /* Throughout this function use _exit() instead of exit(), to
     * avoid calling any atexit() routines of parent.
     */

    if (sigmask_unblock_all () < 0)
        flux_log_error (p->h, "sigprocmask");

    close_parent_fds (p);

    if (!(p->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)) {
        if ((c = zhash_lookup (p->channels, "STDIN"))) {
            if (dup2 (c->child_fd, STDIN_FILENO) < 0) {
                flux_log_error (p->h, "dup2");
                _exit (1);
            }
        }

        if ((c = zhash_lookup (p->channels, "STDOUT"))) {
            if (dup2 (c->child_fd, STDOUT_FILENO) < 0) {
                flux_log_error (p->h, "dup2");
                _exit (1);
            }
        }
        else
            close (STDOUT_FILENO);

        if ((c = zhash_lookup (p->channels, "STDERR"))) {
            if (dup2 (c->child_fd, STDERR_FILENO) < 0) {
                flux_log_error (p->h, "dup2");
                _exit (1);
            }
        }
        else
            close (STDERR_FILENO);
    }

    // Change working directory
    if ((cwd = flux_cmd_getcwd (p->cmd)) && chdir (cwd) < 0) {
        flux_log_error (p->h, "Couldn't change dir to %s: going to /tmp instead", cwd);
        if (chdir ("/tmp") < 0)
            _exit (1);
    }

    // Send ready to parent
    if (local_child_ready (p) < 0)
        _exit (1);

    // Close fds
    if (fdwalk (closefd_child, (void *) p) < 0) {
        flux_log_error (p->h, "Failed closing all fds");
        _exit (1);
    }

    if (p->flags & FLUX_SUBPROCESS_FLAGS_SETPGRP) {
        if (setpgrp () < 0) {
            flux_log_error (p->h, "setpgrp");
            _exit (1);
        }
    }

    environ = flux_cmd_env_expand (p->cmd);
    argv = flux_cmd_argv_expand (p->cmd);
    execvp (argv[0], argv);

    errnum = errno;
    /*
     * NB: close stdout and stderr here to avoid flushing buffers at exit.
     *  This can cause duplicate output if parent was running in fully
     *  bufferred mode, and there was buffered output.
     */
    close (STDOUT_FILENO);
    local_child_report_exec_failed_errno (p, errnum);
    close (STDERR_FILENO);
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
        flux_log_error (p->h, "subprocess_parent_wait_on_child: read");
        return -1;
    }
    return 0;
}

static void child_watch_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg)
{
    flux_subprocess_t *p = arg;
    int status;

    if ((status = flux_child_watcher_get_rstatus (w)) < 0) {
        flux_log_error (p->h, "flux_child_watcher_get_rstatus");
        return;
    }

    p->status = status;

    if (WIFEXITED (p->status) || WIFSIGNALED (p->status)) {

        /* remote/server code may have set EXEC_FAILED or
         * FAILED on fatal errors.
         */
        if (p->state == FLUX_SUBPROCESS_RUNNING) {
            p->state = FLUX_SUBPROCESS_EXITED;
            state_change_start (p);
        }

        /*  Child watcher no longer needed, pid now invalid */
        if (p->child_w)
            flux_watcher_stop (p->child_w);
    }

    if (p->state == FLUX_SUBPROCESS_EXITED)
        subprocess_check_completed (p);
}

static int local_fork (flux_subprocess_t *p)
{
    if ((p->pid = fork ()) < 0)
        return -1;

    if (p->pid == 0)
        local_child (p); /* No return */

    close_child_fds (p);

    /* no-op if reactor is !FLUX_REACTOR_SIGCHLD */
    if (!(p->child_w = flux_child_watcher_create (p->reactor,
                                                  p->pid,
                                                  true,
                                                  child_watch_cb,
                                                  p))) {
        flux_log_error (p->h, "flux_child_watcher_create");
        return -1;
    }

    flux_watcher_start (p->child_w);

    if (subprocess_parent_wait_on_child (p) < 0)
        return -1;

    p->state = FLUX_SUBPROCESS_STARTED;

    return (0);
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
    if ((p->exec_failed_errno = local_release_child (p)) != 0) {
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

        /* spritually FLUX_SUBPROCESS_EXEC_FAILED state at this
         * point */
        errno = p->exec_failed_errno;
        return -1;
    }
    p->state = FLUX_SUBPROCESS_RUNNING;

    return 0;
}

static void start_local_watchers (flux_subprocess_t *p)
{
    struct subprocess_channel *c;

    c = zhash_first (p->channels);
    while (c) {
        flux_watcher_start (c->buffer_write_w);
        flux_watcher_start (c->buffer_read_w);
        c = zhash_next (p->channels);
    }
}

int subprocess_local_setup (flux_subprocess_t *p)
{
    if (local_setup_stdio (p) < 0)
        return -1;
    if (local_setup_channels (p) < 0)
        return -1;
    if (local_fork (p) < 0)
        return -1;
    if (local_exec (p) < 0)
        return -1;
    start_local_watchers (p);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
