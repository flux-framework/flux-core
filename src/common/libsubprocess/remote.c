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
#include <sys/wait.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include <flux/core.h>

#include "ccan/str/str.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/macros.h"
#include "src/common/libutil/llog.h"
#include "src/common/libioencode/ioencode.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command_private.h"
#include "remote.h"
#include "util.h"
#include "client.h"

static void remote_kill_nowait (flux_subprocess_t *p, int signum);

static void set_failed (flux_subprocess_t *p, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    verrprintf (&p->failed_error, fmt, ap);
    p->failed_errno = errno;
    va_end (ap);
}

static void stop_channel_watchers (flux_subprocess_t *p, bool in, bool out)
{
    struct subprocess_channel *c;
    c = zhash_first (p->channels);
    while (c) {
        if (out) {
            flux_watcher_stop (c->out_prep_w);
            flux_watcher_stop (c->out_idle_w);
            flux_watcher_stop (c->out_check_w);
        }
        c = zhash_next (p->channels);
    }
}

static void stop_io_watchers (flux_subprocess_t *p)
{
    stop_channel_watchers (p, true, true);
}

static void stop_in_watchers (flux_subprocess_t *p)
{
    stop_channel_watchers (p, true, false);
}

#if 0
static void stop_out_watchers (flux_subprocess_t *p)
{
    stop_channel_watchers (p, false, true);
}
#endif

static void sigpending_cb (flux_future_t *f, void *arg)
{
    flux_future_t *prev = arg;

    /* fulfill the original future (prev) returned to the caller of
     * flux_subprocess_kill() with the result from the actual
     * remote kill(2) (f).
     */
    flux_future_fulfill_with (prev, f);

    /* Note, f is not destroyed here since it is owned by prev */
}

static void fwd_pending_signal (flux_subprocess_t *p)
{
    flux_future_t *prev = flux_subprocess_aux_get (p, "sp::signal_future");

    if (p->state == FLUX_SUBPROCESS_RUNNING) {
        /* Remote process is now running, send pending signal */
        flux_future_t *f = flux_subprocess_kill (p, p->signal_pending);
        if (!f || (flux_future_then (f, -1., sigpending_cb, prev) < 0))
            flux_future_fulfill_error (prev, errno, NULL);

        /*  If 'prev' is destroyed, then also destroy 'f'. Otherwise,
         *  use-after-free will occur with 'prev' if it is destroyed between
         *  now and when sigpending_cb() is run:
         */
        if (flux_future_aux_set (prev,
                                 NULL,
                                 f,
                                 (flux_free_f) flux_future_destroy) < 0) {
            flux_future_fulfill_error (prev, errno, NULL);
            flux_future_destroy (f);
        }
    }
    else {
        /* Remote process exited or failed, not able to send signal */
        flux_future_fulfill_error (prev, EINVAL, NULL);
    }
    p->signal_pending = 0;

    /*  Now drop the reference on 'prev' added in add_pending_signal().
     *  This may destroy 'f' created above if the caller destroyed 'prev'
     *  before this callback was called.
     */
    flux_future_decref (prev);
}

static void process_new_state (flux_subprocess_t *p,
                               flux_subprocess_state_t state)
{
    if (p->state == FLUX_SUBPROCESS_FAILED)
        return;

    if (state == FLUX_SUBPROCESS_STOPPED) {
        if (p->ops.on_state_change) {
            /* always a chance caller may destroy subprocess in
             * callback */
            subprocess_incref (p);
            (*p->ops.on_state_change) (p, FLUX_SUBPROCESS_STOPPED);
            subprocess_decref (p);
        }
        return;
    }

    p->state = state;

    if (state == FLUX_SUBPROCESS_EXITED) {
        stop_in_watchers (p);
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        stop_io_watchers (p);
    }

    if (p->signal_pending)
        fwd_pending_signal (p);

    if (p->state != p->state_reported)
        state_change_start (p);
}

static bool remote_out_data_available (struct subprocess_channel *c)
{
    /* no need to handle failure states, on fatal error, the
     * io watchers are closed */
    /* N.B. if line buffered and buffer full, gotta flush it
     * regardless if there's a line or not */
    if ((c->line_buffered
         && (fbuf_has_line (c->read_buffer) || !fbuf_space (c->read_buffer)))
        || (!c->line_buffered && fbuf_bytes (c->read_buffer) > 0)
        || (c->read_eof_received && !c->eof_sent_to_caller))
        return true;
    return false;
}

static void remote_out_prep_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    struct subprocess_channel *c = arg;

    if (remote_out_data_available (c))
        flux_watcher_start (c->out_idle_w);
}

static void remote_out_check_cb (flux_reactor_t *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg)
{
    struct subprocess_channel *c = arg;

    flux_watcher_stop (c->out_idle_w);

    /* always a chance caller may destroy subprocess in callback */
    subprocess_incref (c->p);

    if ((c->line_buffered
         && (fbuf_has_line (c->read_buffer)
             || !fbuf_space (c->read_buffer)
             || (c->read_eof_received
                 && fbuf_bytes (c->read_buffer) > 0)))
        || (!c->line_buffered && fbuf_bytes (c->read_buffer) > 0)) {
        c->output_cb (c->p, c->name);
    }

    if (!fbuf_bytes (c->read_buffer)
        && c->read_eof_received
        && !c->eof_sent_to_caller) {
        c->output_cb (c->p, c->name);
        c->eof_sent_to_caller = true;
        c->p->channels_eof_sent++;
    }

    /* no need to handle failure states, on fatal error, the
     * io watchers are closed */
    if (!remote_out_data_available (c) || c->eof_sent_to_caller) {
        /* if no data in buffer, shut down prep/check */
        flux_watcher_stop (c->out_prep_w);
        flux_watcher_stop (c->out_check_w);

        /* close input side as well if eof sent to caller */
        if (c->eof_sent_to_caller)
            c->closed = true;
    }

    if (c->p->state == FLUX_SUBPROCESS_EXITED && c->eof_sent_to_caller)
        subprocess_check_completed (c->p);

    subprocess_decref (c->p);
}

static int remote_channel_setup (flux_subprocess_t *p,
                                 flux_subprocess_output_f output_cb,
                                 const char *name,
                                 int channel_flags)
{
    struct subprocess_channel *c = NULL;
    char *e = NULL;
    int save_errno;

    if (!(c = channel_create (p, output_cb, name, channel_flags))) {
        llog_debug (p, "channel_create: %s", strerror (errno));
        goto error;
    }

    if (channel_flags & CHANNEL_READ) {
        int wflag;

        if ((wflag = cmd_option_line_buffer (p, name)) < 0) {
            llog_debug (p, "cmd_option_line_buffer: %s", strerror (errno));
            goto error;
        }

        if (wflag)
            c->line_buffered = true;

        if (!(p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF)) {
            int buffer_size;
            if ((buffer_size = cmd_option_bufsize (p, name)) < 0) {
                llog_debug (p, "cmd_option_bufsize: %s", strerror (errno));
                goto error;
            }
            if (!(c->read_buffer = fbuf_create (buffer_size))) {
                llog_debug (p, "fbuf_create: %s", strerror (errno));
                goto error;
            }

            if (!(c->out_prep_w = flux_prepare_watcher_create (p->reactor,
                                                               remote_out_prep_cb,
                                                               c))) {
                llog_debug (p, "flux_prepare_watcher_create: %s", strerror (errno));
                goto error;
            }

            if (!(c->out_idle_w = flux_idle_watcher_create (p->reactor,
                                                            NULL,
                                                            c))) {
                llog_debug (p, "flux_idle_watcher_create: %s", strerror (errno));
                goto error;
            }

            if (!(c->out_check_w = flux_check_watcher_create (p->reactor,
                                                              remote_out_check_cb,
                                                              c))) {
                llog_debug (p, "flux_check_watcher_create: %s", strerror (errno));
                goto error;
            }
            /* the output check should be called before other check
             * callbacks, to ensure that the output buffer is emptied
             * before any check callbacks that may move data into the
             * buffer.  So we up the priority of the output check
             * watcher.
             */
            flux_watcher_set_priority (c->out_check_w, 1);
            /* don't start these watchers until we've reached the running
             * state */
        }
        p->channels_eof_expected++;
    }

    if (zhash_insert (p->channels, name, c) < 0) {
        llog_debug (p, "zhash_insert failed");
        errno = EEXIST;
        goto error;
    }
    if (!zhash_freefn (p->channels, name, channel_destroy)) {
        llog_debug (p, "zhash_freefn failed");
        goto error;
    }

    /* now error is in subprocess_free()'s responsibility
     */
    c = NULL;

    free (e);
    return 0;

 error:
    save_errno = errno;
    channel_destroy (c);
    free (e);
    errno = save_errno;
    return -1;
}

static int remote_setup_stdio (flux_subprocess_t *p)
{
    /* stdio is identical to channels, except they are limited to read
     * and/or write, and the buffer's automatically get a NUL char
     * appended on reads */

    if (remote_channel_setup (p,
                              NULL,
                              "stdin",
                              CHANNEL_WRITE) < 0)
        return -1;

    if (p->ops.on_stdout) {
        if (remote_channel_setup (p,
                                  p->ops.on_stdout,
                                  "stdout",
                                  CHANNEL_READ) < 0)
            return -1;
    }

    if (p->ops.on_stderr) {
        if (remote_channel_setup (p,
                                  p->ops.on_stderr,
                                  "stderr",
                                  CHANNEL_READ) < 0)
            return -1;
    }

    return 0;
}

static int remote_setup_channels (flux_subprocess_t *p)
{
    zlist_t *channels = cmd_channel_list (p->cmd);
    const char *name;
    int channel_flags = CHANNEL_READ | CHANNEL_WRITE | CHANNEL_FD;

    if (zlist_size (channels) == 0)
        return 0;

    if (!p->ops.on_channel_out)
        channel_flags &= ~CHANNEL_READ;

    name = zlist_first (channels);
    while (name) {
        if (remote_channel_setup (p,
                                  p->ops.on_channel_out,
                                  name,
                                  channel_flags) < 0)
            return -1;
        name = zlist_next (channels);
    }

    return 0;
}

int subprocess_remote_setup (flux_subprocess_t *p, const char *service_name)
{
    if (remote_setup_stdio (p) < 0)
        return -1;
    if (remote_setup_channels (p) < 0)
        return -1;
    if (!(p->service_name = strdup (service_name)))
        return -1;
    return 0;
}

static int remote_output_local_unbuf (flux_subprocess_t *p,
                                      const char *stream,
                                      const char *data,
                                      int len,
                                      bool eof)
{
    struct subprocess_channel *c;
    int rv = -1;

    /* always a chance caller may destroy subprocess in callback */
    subprocess_incref (p);

    if (!(c = zhash_lookup (p->channels, stream))) {
        llog_debug (p,
                    "Error returning %d bytes received from remote"
                    " subprocess pid %d %s: unknown channel name",
                    len,
                    (int)flux_subprocess_pid (p),
                    stream);
        errno = EPROTO;
        set_failed (p, "error returning unknown channel %s", stream);
        goto out;
    }

    if (data && len) {
        c->unbuf_data = data;
        c->unbuf_len = len;
        if (eof)
            c->read_eof_received = true;
        c->output_cb (c->p, c->name);
    }
    /* N.B. any data not consumed by the user is lost, so if eof is
     * seen, we send it immediately */
    if (eof && !c->eof_sent_to_caller) {
        c->read_eof_received = true;
        c->unbuf_data = NULL;
        c->unbuf_len = 0;

        c->output_cb (c->p, c->name);

        c->eof_sent_to_caller = true;
        c->p->channels_eof_sent++;
    }

    rv = 0;
out:
    subprocess_decref (p);
    return rv;
}

static int remote_output_buffered (flux_subprocess_t *p,
                                   const char *stream,
                                   const char *data,
                                   int len,
                                   bool eof)
{
    struct subprocess_channel *c;

    if (!(c = zhash_lookup (p->channels, stream))) {
        llog_debug (p,
                    "Error buffering %d bytes received from remote"
                    " subprocess pid %d %s: unknown channel name",
                    len,
                    (int)flux_subprocess_pid (p),
                    stream);
        errno = EPROTO;
        set_failed (p, "error buffering unknown channel %s", stream);
        return -1;
    }

    if (data && len) {
        int tmp;

        tmp = fbuf_write (c->read_buffer, data, len);
        if (tmp >= 0 && tmp < len) {
            errno = ENOSPC; // short write is promoted to fatal error
            tmp = -1;
        }
        if (tmp < 0) {
            llog_debug (p,
                        "Error buffering %d bytes received from remote"
                        " subprocess pid %d %s: %s",
                        len,
                        (int)flux_subprocess_pid (p),
                        stream,
                        strerror (errno));
            set_failed (p, "error buffering %d bytes of data", len);
            return -1;
        }
    }
    if (eof) {
        c->read_eof_received = true;
        if (fbuf_readonly (c->read_buffer) < 0)
            llog_debug (p, "fbuf_readonly: %s", strerror (errno));
    }
    if (remote_out_data_available (c)) {
        /* read buffer has stuff in it, start watchers */
        flux_watcher_start (c->out_prep_w);
        flux_watcher_start (c->out_check_w);
    }
    return 0;
}

static void rexec_continuation (flux_future_t *f, void *arg)
{
    flux_subprocess_t *p = arg;
    const char *stream;
    const char *data;
    int len;
    bool eof;

    if (subprocess_rexec_get (f) < 0) {
        if (errno == ENODATA) {
            p->remote_completed = true;
            /* Per RFC42, when remote processes are launched, the
             * process should return finished (i.e. state EXITED)
             * before returning ENODATA.  It is otherwise considered a
             * protocol error.
             *
             * N.B. There is evidence that the sdexec module has
             * violated the protocol before #5956.
             */
            if (p->state != FLUX_SUBPROCESS_EXITED) {
                errno = EPROTO;
                set_failed (p, "%s", strerror (errno));
                goto error;
            }
            subprocess_check_completed (p);
            return;
        }
        set_failed (p, "%s", future_strerror (f, errno));
        goto error;
    }
    if (subprocess_rexec_is_started (f, &p->pid)) {
        p->pid_set = true;
        process_new_state (p, FLUX_SUBPROCESS_RUNNING);
    }
    else if (subprocess_rexec_is_stopped (f)) {
        process_new_state (p, FLUX_SUBPROCESS_STOPPED);
    }
    else if (subprocess_rexec_is_finished (f, &p->status)) {
        process_new_state (p, FLUX_SUBPROCESS_EXITED);
    }
    else if (subprocess_rexec_is_output (f, &stream, &data, &len, &eof)) {
        if (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF) {
            if (remote_output_local_unbuf (p, stream, data, len, eof) < 0)
                goto error;
        }
        else {
            if (remote_output_buffered (p, stream, data, len, eof) < 0)
                goto error;
        }
    }
    flux_future_reset (f);
    return;

error:
    /* c->p->failed_errno and c->p->failed_error expected to be
     * set before this point (typically via set_failed())
     */
    process_new_state (p, FLUX_SUBPROCESS_FAILED);
    remote_kill_nowait (p, SIGKILL);
}

int remote_exec (flux_subprocess_t *p)
{
    flux_future_t *f;
    int flags = 0;

    if (zlist_size (cmd_channel_list (p->cmd)) > 0)
        flags |= SUBPROCESS_REXEC_CHANNEL;
    if (p->ops.on_stdout)
        flags |= SUBPROCESS_REXEC_STDOUT;
    if (p->ops.on_stderr)
        flags |= SUBPROCESS_REXEC_STDERR;

    if (!(f = subprocess_rexec (p->h, p->service_name, p->rank, p->cmd, flags))
        || flux_future_then (f, -1., rexec_continuation, p) < 0) {
        llog_debug (p,
                    "error sending rexec.exec request: %s",
                    strerror (errno));
        flux_future_destroy (f);
        return -1;
    }
    p->f = f;
    return 0;
}

flux_future_t *remote_kill (flux_subprocess_t *p, int signum)
{
    return subprocess_kill (p->h, p->service_name, p->rank, p->pid, signum);
}

static void remote_kill_nowait (flux_subprocess_t *p, int signum)
{
    if (p->pid_set) {
        flux_future_t *f;
        f = remote_kill (p, signum);
        flux_future_destroy (f);
    }
}

/*
 * vi: ts=4 sw=4 expandtab
 */
