/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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

#include <sys/types.h>
#include <sys/socket.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/fdutils.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "local.h"
#include "fork.h"
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
        c->buffer_read_w_started = false;

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
    if (revents & FLUX_POLLIN) {
        bool eof_set = false;
        if (!c->eof_sent_to_caller) {
            flux_buffer_t *fb;

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
    }
    else
        flux_log_error (c->p->h, "flux_buffer_read_watcher on %s: 0x%X:",
                        c->name, revents);

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
                                int channel_flags)
{
    struct subprocess_channel *c = NULL;
    int fds[2] = { -1, -1 };
    char *e = NULL;
    int save_errno;
    int fd_flags;
    int buffer_size;

    if (!(c = channel_create (p, output_f, name, channel_flags))) {
        flux_log (p->h, LOG_DEBUG, "channel_create");
        goto error;
    }

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, fds) < 0) {
        flux_log (p->h, LOG_DEBUG, "socketpair");
        goto error;
    }

    c->parent_fd = fds[0];
    c->child_fd = fds[1];

    /* set fds[] to -1, on error is now subprocess_free()'s
     * responsibility
     */
    fds[0] = -1;
    fds[1] = -1;

    if ((fd_flags = fd_set_nonblocking (c->parent_fd)) < 0) {
        flux_log (p->h, LOG_DEBUG, "fd_set_nonblocking");
        goto error;
    }

    if ((buffer_size = cmd_option_bufsize (p, name)) < 0) {
        flux_log (p->h, LOG_DEBUG, "cmd_option_bufsize");
        goto error;
    }

    if ((channel_flags & CHANNEL_WRITE) && in_cb) {
        c->buffer_write_w = flux_buffer_write_watcher_create (p->reactor,
                                                              c->parent_fd,
                                                              buffer_size,
                                                              in_cb,
                                                              0,
                                                              c);
        if (!c->buffer_write_w) {
            flux_log (p->h, LOG_DEBUG, "flux_buffer_write_watcher_create");
            goto error;
        }
    }

    if ((channel_flags & CHANNEL_READ) && out_cb) {
        int wflag;

        if ((wflag = cmd_option_line_buffer (p, name)) < 0) {
            flux_log (p->h, LOG_DEBUG, "cmd_option_line_buffer");
            goto error;
        }

        if (wflag)
            c->line_buffered = true;

        c->buffer_read_w = flux_buffer_read_watcher_create (p->reactor,
                                                            c->parent_fd,
                                                            buffer_size,
                                                            out_cb,
                                                            wflag,
                                                            c);
        if (!c->buffer_read_w) {
            flux_log (p->h, LOG_DEBUG, "flux_buffer_read_watcher_create");
            goto error;
        }

        p->channels_eof_expected++;
    }

    if (channel_flags & CHANNEL_FD) {
        if (asprintf (&e, "%s", name) < 0) {
            flux_log (p->h, LOG_DEBUG, "asprintf");
            goto error;
        }

        /* set overwrite flag, if caller recursively launches
         * another subprocess */
        if (flux_cmd_setenvf (p->cmd,
                              1,
                              e,
                              "%d",
                              c->child_fd) < 0) {
            flux_log (p->h, LOG_DEBUG, "flux_cmd_setenvf");
            goto error;
        }
    }

    if (zhash_insert (p->channels, name, c) < 0) {
        flux_log (p->h, LOG_DEBUG, "zhash_insert");
        goto error;
    }
    if (!zhash_freefn (p->channels, name, channel_destroy)) {
        flux_log (p->h, LOG_DEBUG, "zhash_freefn");
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
    if (p->flags & FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH)
        return 0;

    /* stdio is identical to channels, except they are limited to read
     * and/or write, and the buffer's automatically get a NUL char
     * appended on reads */

    if (channel_local_setup (p,
                             NULL,
                             local_in_cb,
                             NULL,
                             "stdin",
                             CHANNEL_WRITE) < 0)
        return -1;

    if (p->ops.on_stdout) {
        if (channel_local_setup (p,
                                 p->ops.on_stdout,
                                 NULL,
                                 local_stdout_cb,
                                 "stdout",
                                 CHANNEL_READ) < 0)
            return -1;
    }

    if (p->ops.on_stderr) {
        if (channel_local_setup (p,
                                 p->ops.on_stderr,
                                 NULL,
                                 local_stderr_cb,
                                 "stderr",
                                 CHANNEL_READ) < 0)
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
        flux_log (p->h, LOG_DEBUG, "flux_cmd_channel_list");
        return -1;
    }

    if (!(len = zlist_size (channels)))
        return 0;

    if (!p->ops.on_channel_out)
        channel_flags &= ~CHANNEL_READ;

    name = zlist_first (channels);
    while (name) {
        if (channel_local_setup (p,
                                 p->ops.on_channel_out,
                                 local_in_cb,
                                 p->ops.on_channel_out ? local_out_cb : NULL,
                                 name,
                                 channel_flags) < 0)
            return -1;
        name = zlist_next (channels);
    }

    return 0;
}

static void close_child_fds (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    c = zhash_first (p->channels);
    while (c) {
        if (c->child_fd != -1) {
            close (c->child_fd);
            c->child_fd = -1;
        }
        c = zhash_next (p->channels);
    }
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

static int create_process (flux_subprocess_t *p)
{
    return create_process_fork (p);
}

static int start_local_watchers (flux_subprocess_t *p)
{
    struct subprocess_channel *c;

    /* no-op if reactor is !FLUX_REACTOR_SIGCHLD */
    if (!(p->child_w = flux_child_watcher_create (p->reactor,
                                                  p->pid,
                                                  true,
                                                  child_watch_cb,
                                                  p))) {
        flux_log (p->h, LOG_DEBUG, "flux_child_watcher_create");
        return -1;
    }
    flux_watcher_start (p->child_w);

    c = zhash_first (p->channels);
    while (c) {
        int ret;
        flux_watcher_start (c->buffer_write_w);
        if ((ret = cmd_option_stream_stop (p, c->name)) < 0)
            return -1;
        if (ret) {
            flux_watcher_start (c->buffer_read_stopped_w);
        }
        else {
            flux_watcher_start (c->buffer_read_w);
            c->buffer_read_w_started = true;
        }
        c = zhash_next (p->channels);
    }
    return 0;
}

int subprocess_local_setup (flux_subprocess_t *p)
{
    if (local_setup_stdio (p) < 0)
        return -1;
    if (local_setup_channels (p) < 0)
        return -1;
    if (create_process (p) < 0)
        return -1;

    p->state = FLUX_SUBPROCESS_RUNNING;
    close_child_fds (p);

    if (p->hooks.post_fork) {
        p->in_hook = true;
        (*p->hooks.post_fork) (p, p->hooks.post_fork_arg);
        p->in_hook = false;
    }

    if (start_local_watchers (p) < 0)
        return -1;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
