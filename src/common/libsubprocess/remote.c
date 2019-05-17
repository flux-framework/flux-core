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
#    include "config.h"
#endif

#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>
#include <sodium.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/macros.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "remote.h"
#include "util.h"

static void start_channel_watchers (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    c = zhash_first (p->channels);
    while (c) {
        flux_watcher_start (c->in_prep_w);
        flux_watcher_start (c->in_check_w);
        flux_watcher_start (c->out_prep_w);
        flux_watcher_start (c->out_check_w);
        c = zhash_next (p->channels);
    }
}

static void stop_channel_watchers (flux_subprocess_t *p, bool in, bool out)
{
    struct subprocess_channel *c;
    c = zhash_first (p->channels);
    while (c) {
        if (in) {
            flux_watcher_stop (c->in_prep_w);
            flux_watcher_stop (c->in_idle_w);
            flux_watcher_stop (c->in_check_w);
        }
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

static void process_new_state (flux_subprocess_t *p,
                               flux_subprocess_state_t state,
                               int rank,
                               pid_t pid,
                               int errnum,
                               int status)
{
    if (p->state == FLUX_SUBPROCESS_EXEC_FAILED
        || p->state == FLUX_SUBPROCESS_FAILED)
        return;

    p->state = state;

    if (p->state == FLUX_SUBPROCESS_STARTED) {
        p->pid = pid;
        p->pid_set = true;
    } else if (p->state == FLUX_SUBPROCESS_RUNNING) {
        start_channel_watchers (p);
    } else if (state == FLUX_SUBPROCESS_EXEC_FAILED) {
        p->exec_failed_errno = errnum;
        stop_io_watchers (p);
    } else if (state == FLUX_SUBPROCESS_EXITED) {
        p->status = status;
        stop_in_watchers (p);
    } else if (state == FLUX_SUBPROCESS_FAILED) {
        p->failed_errno = errnum;
        stop_io_watchers (p);
    }

    if (p->state != p->state_reported)
        state_change_start (p);
}

static void remote_in_prep_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    struct subprocess_channel *c = arg;

    if (flux_buffer_bytes (c->write_buffer) > 0
        || (c->closed && !c->write_eof_sent)
        || (c->p->state == FLUX_SUBPROCESS_EXITED
            || c->p->state == FLUX_SUBPROCESS_FAILED))
        flux_watcher_start (c->in_idle_w);
}

static int remote_write (struct subprocess_channel *c)
{
    flux_future_t *f = NULL;
    const void *ptr;
    char *s_data = NULL;
    int lenp, s_len;
    int rv = -1;

    if (!(ptr = flux_buffer_read (c->write_buffer, -1, &lenp))) {
        flux_log_error (c->p->h, "flux_buffer_read");
        goto error;
    }

    s_len = sodium_base64_encoded_len (lenp, sodium_base64_VARIANT_ORIGINAL);

    if (!(s_data = calloc (1, s_len))) {
        flux_log_error (c->p->h, "calloc");
        goto error;
    }

    sodium_bin2base64 (s_data,
                       s_len,
                       ptr,
                       lenp,
                       sodium_base64_VARIANT_ORIGINAL);

    if (!(f = flux_rpc_pack (c->p->h,
                             "cmb.rexec.write",
                             c->p->rank,
                             FLUX_RPC_NORESPONSE,
                             "{ s:i s:s s:s s:i }",
                             "pid",
                             c->p->pid,
                             "name",
                             c->name,
                             "data",
                             s_data,
                             "close",
                             0))) {
        flux_log_error (c->p->h, "flux_rpc_pack");
        return -1;
    }

    rv = 0;
error:
    /* no response */
    flux_future_destroy (f);
    free (s_data);
    return rv;
}

static int remote_close (struct subprocess_channel *c)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (c->p->h,
                             "cmb.rexec.write",
                             c->p->rank,
                             FLUX_RPC_NORESPONSE,
                             "{ s:i s:s s:i }",
                             "pid",
                             c->p->pid,
                             "name",
                             c->name,
                             "close",
                             1))) {
        flux_log_error (c->p->h, "flux_rpc_pack");
        return -1;
    }

    /* no response */
    flux_future_destroy (f);

    /* No need to do a "channel_flush", normal io reactor will handle
     * flush of any data in read buffer */
    return 0;
}

static void remote_in_check_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    struct subprocess_channel *c = arg;
    flux_future_t *fkill;

    flux_watcher_stop (c->in_idle_w);

    if (flux_buffer_bytes (c->write_buffer) > 0) {
        if (remote_write (c) < 0) {
            flux_log_error (c->p->h, "remote_write");
            goto error;
        }
    }

    if (!flux_buffer_bytes (c->write_buffer) && c->closed
        && !c->write_eof_sent) {
        if (remote_close (c) < 0) {
            flux_log_error (c->p->h, "remote_close");
            goto error;
        }
        c->write_eof_sent = true;
    }

    if (c->write_eof_sent || c->p->state == FLUX_SUBPROCESS_EXITED
        || c->p->state == FLUX_SUBPROCESS_FAILED) {
        flux_watcher_stop (c->in_prep_w);
        flux_watcher_stop (c->in_check_w);
    }

    return;

error:
    process_new_state (c->p, FLUX_SUBPROCESS_FAILED, c->p->rank, -1, errno, 0);
    if (!(fkill = remote_kill (c->p, SIGKILL)))
        flux_log_error (c->p->h, "%s: remote_kill", __FUNCTION__);
    else
        flux_future_destroy (fkill);
    flux_future_destroy (c->p->f);
    c->p->f = NULL;
}

static void remote_out_prep_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    struct subprocess_channel *c = arg;

    /* no need to handle failure states, on fatal error, these
     * reactors are closed */
    if (flux_buffer_bytes (c->read_buffer) > 0
        || (c->read_eof_received && !c->eof_sent_to_caller))
        flux_watcher_start (c->out_idle_w);
}

static void remote_out_check_cb (flux_reactor_t *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg)
{
    struct subprocess_channel *c = arg;

    flux_watcher_stop (c->out_idle_w);

    if (flux_buffer_bytes (c->read_buffer) > 0) {
        c->output_f (c->p, c->name);
    }

    if (!flux_buffer_bytes (c->read_buffer) && c->read_eof_received
        && !c->eof_sent_to_caller) {
        c->output_f (c->p, c->name);
        c->eof_sent_to_caller = true;
        c->p->channels_eof_sent++;
    }

    /* no need to handle failure states, on fatal error, these
     * reactors are closed */
    if (c->eof_sent_to_caller) {
        flux_watcher_stop (c->out_prep_w);
        flux_watcher_stop (c->out_check_w);

        /* close input side as well */
        flux_watcher_stop (c->in_prep_w);
        flux_watcher_stop (c->in_idle_w);
        flux_watcher_stop (c->in_check_w);
        c->closed = true;
    }

    if (c->p->state == FLUX_SUBPROCESS_EXITED && c->eof_sent_to_caller)
        subprocess_check_completed (c->p);
}

static int remote_channel_setup (flux_subprocess_t *p,
                                 flux_subprocess_output_f output_f,
                                 const char *name,
                                 int channel_flags,
                                 int buffer_size)
{
    struct subprocess_channel *c = NULL;
    char *e = NULL;
    int save_errno;

    if (!(c = channel_create (p, output_f, name, channel_flags))) {
        flux_log_error (p->h, "calloc");
        goto error;
    }

    if (channel_flags & CHANNEL_WRITE) {
        if (!(c->write_buffer = flux_buffer_create (buffer_size))) {
            flux_log_error (p->h, "flux_buffer_create");
            goto error;
        }

        if (!(c->in_prep_w = flux_prepare_watcher_create (p->reactor,
                                                          remote_in_prep_cb,
                                                          c))) {
            flux_log_error (p->h, "flux_prepare_watcher_create");
            goto error;
        }

        if (!(c->in_idle_w = flux_idle_watcher_create (p->reactor, NULL, c))) {
            flux_log_error (p->h, "flux_idle_watcher_create");
            goto error;
        }

        if (!(c->in_check_w = flux_check_watcher_create (p->reactor,
                                                         remote_in_check_cb,
                                                         c))) {
            flux_log_error (p->h, "flux_check_watcher_create");
            goto error;
        }

        /* do not start these watchers till later, cannot send data to
         * remote until it has reached running state
         */
    }

    if (channel_flags & CHANNEL_READ) {
        if (!(c->read_buffer = flux_buffer_create (buffer_size))) {
            flux_log_error (p->h, "flux_buffer_create");
            goto error;
        }
        p->channels_eof_expected++;

        if (!(c->out_prep_w = flux_prepare_watcher_create (p->reactor,
                                                           remote_out_prep_cb,
                                                           c))) {
            flux_log_error (p->h, "flux_prepare_watcher_create");
            goto error;
        }

        if (!(c->out_idle_w = flux_idle_watcher_create (p->reactor, NULL, c))) {
            flux_log_error (p->h, "flux_idle_watcher_create");
            goto error;
        }

        if (!(c->out_check_w = flux_check_watcher_create (p->reactor,
                                                          remote_out_check_cb,
                                                          c))) {
            flux_log_error (p->h, "flux_check_watcher_create");
            goto error;
        }

        /* don't start these watchers until we've reached the running
         * state */
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
    channel_destroy (c);
    free (e);
    errno = save_errno;
    return -1;
}

static int remote_setup_stdio (flux_subprocess_t *p)
{
    int buffer_size;

    /* stdio is identical to channels, except they are limited to read
     * and/or write, and the buffer's automatically get a NUL char
     * appended on reads */

    if ((buffer_size = cmd_option_bufsize (p, "STDIN")) < 0)
        return -1;

    if (remote_channel_setup (p, NULL, "STDIN", CHANNEL_WRITE, buffer_size) < 0)
        return -1;

    if (p->ops.on_stdout) {
        if ((buffer_size = cmd_option_bufsize (p, "STDOUT")) < 0)
            return -1;

        if (remote_channel_setup (p,
                                  p->ops.on_stdout,
                                  "STDOUT",
                                  CHANNEL_READ,
                                  buffer_size)
            < 0)
            return -1;
    }

    if (p->ops.on_stderr) {
        if ((buffer_size = cmd_option_bufsize (p, "STDERR")) < 0)
            return -1;

        if (remote_channel_setup (p,
                                  p->ops.on_stderr,
                                  "STDERR",
                                  CHANNEL_READ,
                                  buffer_size)
            < 0)
            return -1;
    }

    return 0;
}

static int remote_setup_channels (flux_subprocess_t *p)
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

        if (remote_channel_setup (p,
                                  p->ops.on_channel_out,
                                  name,
                                  channel_flags,
                                  buffer_size)
            < 0)
            return -1;
        name = zlist_next (channels);
    }

    return 0;
}

int subprocess_remote_setup (flux_subprocess_t *p)
{
    if (remote_setup_stdio (p) < 0)
        return -1;
    if (remote_setup_channels (p) < 0)
        return -1;
    return 0;
}

static int remote_state (flux_subprocess_t *p, flux_future_t *f, int rank)
{
    flux_subprocess_state_t state;
    pid_t pid = -1;
    int errnum = 0;
    int status = 0;

    if (flux_rpc_get_unpack (f, "{ s:i }", "state", &state) < 0) {
        flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        return -1;
    }

    if (state == FLUX_SUBPROCESS_STARTED) {
        if (flux_rpc_get_unpack (f, "{ s:i }", "pid", &pid) < 0) {
            flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
            return -1;
        }
    }

    if (state == FLUX_SUBPROCESS_EXEC_FAILED
        || state == FLUX_SUBPROCESS_FAILED) {
        if (flux_rpc_get_unpack (f, "{ s:i }", "errno", &errnum) < 0) {
            flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
            return -1;
        }
    }

    if (state == FLUX_SUBPROCESS_EXITED) {
        if (flux_rpc_get_unpack (f, "{ s:i }", "status", &status) < 0) {
            flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
            return -1;
        }
    }

    process_new_state (p, state, rank, pid, errnum, status);

    return 0;
}

static int remote_output (flux_subprocess_t *p,
                          flux_future_t *f,
                          int rank,
                          pid_t pid)
{
    struct subprocess_channel *c;
    const char *s_data;
    char *data = NULL;
    size_t s_len, len, tmp;
    const char *stream;
    int eof;
    int rv = -1;

    if (flux_rpc_get_unpack (f, "{ s:s }", "stream", &stream)) {
        flux_log_error (p->h, "flux_rpc_get_unpack EPROTO stream");
        goto cleanup;
    }

    if (!(c = zhash_lookup (p->channels, stream))) {
        flux_log_error (p->h,
                        "invalid channel received: rank = %d, pid = %d, stream "
                        "= %s",
                        rank,
                        pid,
                        stream);
        errno = EPROTO;
        goto cleanup;
    }

    if (!flux_rpc_get_unpack (f, "{ s:s }", "data", &s_data)) {
        s_len = strlen (s_data);
        len = BASE64_DECODE_SIZE (s_len);

        if (!(data = calloc (1, len))) {
            flux_log_error (p->h, "calloc");
            goto cleanup;
        }

        if (sodium_base642bin ((unsigned char *)data,
                               len,
                               s_data,
                               s_len,
                               NULL,
                               &len,
                               NULL,
                               sodium_base64_VARIANT_ORIGINAL)
            < 0) {
            flux_log_error (p->h, "sodium_base642bin");
            goto cleanup;
        }

        if ((tmp = flux_buffer_write (c->read_buffer, data, len)) < 0) {
            flux_log_error (p->h, "flux_buffer_write");
            goto cleanup;
        }

        /* add list of msgs if there is overflow? */

        if (tmp != len) {
            flux_log_error (p->h,
                            "channel buffer error: rank = %d pid = %d, stream "
                            "= %s, "
                            "len = %zu",
                            rank,
                            pid,
                            stream,
                            len);
            errno = EOVERFLOW;
            goto cleanup;
        }
    } else if (!flux_rpc_get_unpack (f, "{ s:i }", "eof", &eof)) {
        c->read_eof_received = true;
    }

    rv = 0;
cleanup:
    free (data);
    return rv;
}

static void remote_completion (flux_subprocess_t *p)
{
    p->remote_completed = true;
    /* TBON inorder delivery of messages should guarantee we received
     * FLUX_SUBPROCESS_EXITED before this.
     */
    subprocess_check_completed (p);
}

static void remote_exec_cb (flux_future_t *f, void *arg)
{
    flux_subprocess_t *p = arg;
    const char *type;
    int rank;
    pid_t pid;

    if (flux_rpc_get_unpack (f, "{ s:s s:i }", "type", &type, "rank", &rank)
        < 0) {
        flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    if (!strcmp (type, "state")) {
        if (remote_state (p, f, rank) < 0)
            goto error;
        if (p->state == FLUX_SUBPROCESS_EXEC_FAILED
            || p->state == FLUX_SUBPROCESS_FAILED) {
            flux_future_destroy (f);
            p->f = NULL;
        } else
            flux_future_reset (f);
    } else if (!strcmp (type, "output")) {
        if (flux_rpc_get_unpack (f, "{ s:i }", "pid", &pid) < 0) {
            flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
            goto error;
        }
        if (remote_output (p, f, rank, pid) < 0)
            goto error;
        flux_future_reset (f);
    } else if (!strcmp (type, "complete")) {
        remote_completion (p);
        flux_future_destroy (f);
        p->f = NULL;
    } else {
        flux_log_error (p->h, "%s: EPROTO", __FUNCTION__);
        errno = EPROTO;
        goto error;
    }

    return;

error:
    if (p->state == FLUX_SUBPROCESS_RUNNING) {
        flux_future_t *fkill;
        if (!(fkill = remote_kill (p, SIGKILL)))
            flux_log_error (p->h, "%s: remote_kill", __FUNCTION__);
        else
            flux_future_destroy (fkill);
    }
    process_new_state (p, FLUX_SUBPROCESS_FAILED, p->rank, -1, errno, 0);
    flux_future_destroy (f);
    p->f = NULL;
}

static void remote_continuation_cb (flux_future_t *f, void *arg)
{
    flux_subprocess_t *p = arg;
    const char *type;
    int rank;
    int save_errno;

    if (flux_rpc_get_unpack (f, "{ s:s s:i }", "type", &type, "rank", &rank)
        < 0) {
        flux_log_error (p->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    if (!strcmp (type, "start")) {
        flux_future_reset (f);
        if (flux_future_then (f, -1., remote_exec_cb, p) < 0) {
            flux_log_error (p->h, "flux_future_then");
            goto error;
        }
    } else {
        flux_log_error (p->h, "%s: EPROTO", __FUNCTION__);
        errno = EPROTO;
        goto error;
    }

    return;

error:
    /* error here is fatal, we can't do anything else b/c we lack a
     * PID or anything similar.
     */
    process_new_state (p, FLUX_SUBPROCESS_FAILED, p->rank, -1, errno, 0);
    save_errno = errno;
    flux_future_destroy (p->f);
    p->f = NULL;
    errno = save_errno;
    return;
}

int remote_exec (flux_subprocess_t *p)
{
    flux_future_t *f = NULL;
    char *cmd_str = NULL;
    int save_errno;

    if (!(cmd_str = flux_cmd_tojson (p->cmd))) {
        flux_log_error (p->h, "flux_cmd_tojson");
        goto error;
    }

    /* completion & state_change cbs always required b/c we use it
     * internally in this code.  But output callbacks are optional, we
     * don't care if user doesn't want it.
     */
    if (!(f = flux_rpc_pack (p->h,
                             "cmb.rexec",
                             p->rank,
                             0,
                             "{s:s s:i s:i s:i}",
                             "cmd",
                             cmd_str,
                             "on_channel_out",
                             p->ops.on_channel_out ? 1 : 0,
                             "on_stdout",
                             p->ops.on_stdout ? 1 : 0,
                             "on_stderr",
                             p->ops.on_stderr ? 1 : 0))) {
        flux_log_error (p->h, "flux_rpc");
        goto error;
    }

    if (flux_future_then (f, -1., remote_continuation_cb, p) < 0) {
        flux_log_error (p->h, "flux_future_then");
        goto error;
    }

    p->f = f;
    free (cmd_str);
    return 0;

error:
    save_errno = errno;
    flux_future_destroy (f);
    free (cmd_str);
    errno = save_errno;
    return -1;
}

flux_future_t *remote_kill (flux_subprocess_t *p, int signum)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (p->h,
                             "cmb.rexec.signal",
                             p->rank,
                             0,
                             "{s:i s:i}",
                             "pid",
                             p->pid,
                             "signum",
                             signum))) {
        flux_log_error (p->h, "%s: flux_rpc_pack", __FUNCTION__);
        return NULL;
    }
    return f;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
