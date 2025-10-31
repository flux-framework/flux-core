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
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command_private.h"
#include "local.h"
#include "remote.h"
#include "client.h"
#include "util.h"
#include "sigchld.h"
#include "msgchan.h"

/*
 * Primary Structures
 */

void channel_destroy (void *arg)
{
    struct subprocess_channel *c = arg;
    if (c) {
        int saved_errno = errno;
        free (c->name);

        if (c->parent_fd != -1)
            close (c->parent_fd);
        if (c->child_fd != -1)
            close (c->child_fd);
        flux_watcher_destroy (c->buffer_write_w);
        flux_watcher_destroy (c->buffer_read_w);
        flux_watcher_destroy (c->buffer_read_stopped_w);
        c->buffer_read_w_started = false;

        fbuf_destroy (c->read_buffer);
        flux_watcher_destroy (c->out_prep_w);
        flux_watcher_destroy (c->out_idle_w);
        flux_watcher_destroy (c->out_check_w);

        free (c);
        errno = saved_errno;
    }
}

struct subprocess_channel *channel_create (flux_subprocess_t *p,
                                           flux_subprocess_output_f output_cb,
                                           const char *name,
                                           int flags)
{
    struct subprocess_channel *c;

    if (!(c = calloc (1, sizeof (*c))))
        return NULL;
    c->p = p;
    c->output_cb = output_cb;
    c->parent_fd = -1;
    c->child_fd = -1;
    if (!(c->name = strdup (name)))
        goto error;
    c->flags = flags;
    return c;
error:
    channel_destroy (c);
    return NULL;
}

/*  Return the set of valid child file descriptors as an idset
 */
struct idset *subprocess_childfds (flux_subprocess_t *p)
{
    struct subprocess_channel *c;
    struct msgchan *mch;
    struct idset *ids;
    const char *stdchan[] = { "stdin", "stdout", "stderr" };

    /*  fds 0,1,2 always remain open in the child (stdin,out,err)
     */
    if (!(ids = idset_decode ("0-2")))
        return NULL;

    if (p->sync_fds[1] > 0)
        idset_set (ids, p->sync_fds[1]);

    c = zhash_first (p->channels);
    while (c) {
        // invalidate (don't protect) file descriptors that are duped to stdio
        for (int i = 0; i < ARRAY_SIZE (stdchan); i++) {
            if (streq (c->name, stdchan[i]))
                goto next;
        }
        idset_set (ids, c->child_fd);
next:
        c = zhash_next (p->channels);
    }

    // protect any message channel file descriptors to be passed to subproc
    mch = zhash_first (p->msgchans);
    while (mch) {
        idset_set (ids, msgchan_get_fd (mch));
        mch = zhash_next (p->msgchans);
    }

    return ids;
}

static void subprocess_free (flux_subprocess_t *p)
{
    if (p) {
        int saved_errno = errno;
        flux_cmd_destroy (p->cmd);

        aux_destroy (&p->aux);
        if (p->channels)
            zhash_destroy (&p->channels);
        zhash_destroy (&p->msgchans);

        close_pair_fds (p->sync_fds);

        flux_watcher_destroy (p->state_prep_w);
        flux_watcher_destroy (p->state_idle_w);
        flux_watcher_destroy (p->state_check_w);

        flux_watcher_destroy (p->completed_prep_w);
        flux_watcher_destroy (p->completed_idle_w);
        flux_watcher_destroy (p->completed_check_w);

        if (p->f)
            flux_future_destroy (p->f);
        free (p->service_name);

        if (p->has_sigchld_ctx) {
            sigchld_unregister (p->pid); // this is a no-op if already done
            sigchld_finalize ();
        }

        free (p);
        errno = saved_errno;
    }
}

static flux_subprocess_t *subprocess_create (
    flux_t *h,
    flux_reactor_t *r,
    int flags,
    const flux_cmd_t *cmd,
    const flux_subprocess_ops_t *ops,
    const flux_subprocess_hooks_t *hooks,
    int rank,
    bool local,
    subprocess_log_f log_fn,
    void *log_data)
{
    flux_subprocess_t *p = calloc (1, sizeof (*p));

    if (!p)
        return NULL;

    if (local) {
        if (sigchld_initialize (r) < 0)
            goto error;
        p->has_sigchld_ctx = true;
    }

    p->llog = log_fn;
    p->llog_data = log_data;

    /* init fds, so on error we don't accidentally close stdin
     * (i.e. fd == 0)
     */
    init_pair_fds (p->sync_fds);

    /* set CLOEXEC on sync_fds, so on exec(), child sync_fd is closed
     * and seen by parent */
#if SOCK_CLOEXEC
    if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, p->sync_fds) < 0)
        goto error;
#else
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, p->sync_fds) < 0
        || fd_set_cloexec (p->sync_fds[0]) < 0
        || fd_set_cloexec (p->sync_fds[1]) < 0)
        goto error;
#endif

    if (!(p->channels = zhash_new ())
        || !(p->msgchans = zhash_new ()))
        goto error;

    p->state = FLUX_SUBPROCESS_INIT;
    p->state_reported = p->state;

    if (!(p->cmd = flux_cmd_copy (cmd)))
        goto error;

    if (ops)
        p->ops = *ops;

    if (hooks)
        p->hooks = *hooks;

    p->h = h;
    p->reactor = r;
    p->rank = rank;
    p->local = local;

    p->flags = flags;

    p->refcount = 1;
    return (p);

error:
    subprocess_free (p);
    return NULL;
}

/*
 * Accessors
 */

int subprocess_status (flux_subprocess_t *p)
{
    assert (p);
    return p->status;
}

void subprocess_standard_output (flux_subprocess_t *p, const char *stream)
{
    /* everything except stderr goes to stdout */
    FILE *fstream = !strcasecmp (stream, "stderr") ? stderr : stdout;
    const char *buf;
    int len;

    /* Do not use flux_subprocess_getline(), this should work
     * regardless if stream is line buffered or not */

    if ((len = flux_subprocess_read_line (p, stream, &buf)) < 0) {
        log_err ("subprocess_standard_output: read_line");
        return;
    }

    /* we're at the end of the stream, read any lingering data */
    if (len == 0 && flux_subprocess_read_stream_closed (p, stream)) {
        if ((len = flux_subprocess_read (p, stream, &buf)) < 0) {
            log_err ("subprocess_standard_output: read");
            return;
        }
    }

    if (len)
        fwrite (buf, len, 1, fstream);
}

/*
 *  Process handling:
 */

void subprocess_check_completed (flux_subprocess_t *p)
{
    if (p->state != FLUX_SUBPROCESS_EXITED) {
        log_err ("subprocess_check_completed: unexpected state %s",
                 flux_subprocess_state_string (p->state));
        return;
    }

    /* we're also waiting for the "complete" to come from the remote end */
    if (!p->local && !p->remote_completed)
        return;

    if (p->completed)
        return;

    if (p->channels_eof_sent == p->channels_eof_expected) {
        p->completed = true;
        flux_watcher_start (p->completed_prep_w);
        flux_watcher_start (p->completed_check_w);
    }
}

void state_change_start (flux_subprocess_t *p)
{
    if (p->ops.on_state_change) {
        flux_watcher_start (p->state_prep_w);
        flux_watcher_start (p->state_check_w);
    }
}

static void state_change_prep_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg)
{
    flux_subprocess_t *p = arg;

    if (p->state_reported != p->state)
        flux_watcher_start (p->state_idle_w);
}

static flux_subprocess_state_t state_change_next (flux_subprocess_t *p)
{
    /* N.B. possible transition to FLUX_SUBPROCESS_STOPPED not handled
     * here, see issue #5083
     */
    assert (p->state_reported != p->state);
    assert (p->state_reported == FLUX_SUBPROCESS_INIT
            || p->state_reported == FLUX_SUBPROCESS_RUNNING);

    if (p->state_reported == FLUX_SUBPROCESS_INIT)
        /* next state must be RUNNING */
        return FLUX_SUBPROCESS_RUNNING;
    else if (p->state_reported == FLUX_SUBPROCESS_RUNNING)
        /* next state is EXITED */
        return FLUX_SUBPROCESS_EXITED;
    /* shouldn't be possible to reach here */
    return p->state_reported;
}

static void state_change_check_cb (flux_reactor_t *r,
                                   flux_watcher_t *w,
                                   int revents,
                                   void *arg)
{
    flux_subprocess_t *p = arg;
    flux_subprocess_state_t next_state = FLUX_SUBPROCESS_INIT;

    flux_watcher_stop (p->state_idle_w);

    /* always a chance caller may destroy subprocess in callback */
    subprocess_incref (p);

    if (p->state_reported != p->state) {
        /* this is the ubiquitous fail state for internal failures,
         * any state can jump to this state.  Even if some state changes
         * occurred in between, we'll jump to this state.
         */
        if (p->state == FLUX_SUBPROCESS_FAILED)
            next_state = FLUX_SUBPROCESS_FAILED;
        else
            next_state = state_change_next (p);

        (*p->ops.on_state_change) (p, next_state);
        p->state_reported = next_state;
    }

    /* once we hit one of these states, no more state changes */
    if (p->state_reported == FLUX_SUBPROCESS_EXITED
        || p->state_reported == FLUX_SUBPROCESS_FAILED) {
        flux_watcher_stop (p->state_prep_w);
        flux_watcher_stop (p->state_check_w);
    }
    else if (p->state == p->state_reported) {
        flux_watcher_stop (p->state_prep_w);
        flux_watcher_stop (p->state_check_w);
    }

    if (p->state_reported == FLUX_SUBPROCESS_EXITED)
        subprocess_check_completed (p);

    subprocess_decref (p);
}

static int subprocess_setup_state_change (flux_subprocess_t *p)
{
    if (p->ops.on_state_change) {
        p->state_prep_w = flux_prepare_watcher_create (p->reactor,
                                                       state_change_prep_cb,
                                                       p);
        if (!p->state_prep_w) {
            log_err ("flux_prepare_watcher_create");
            return -1;
        }

        p->state_idle_w = flux_idle_watcher_create (p->reactor,
                                                    NULL,
                                                    p);
        if (!p->state_idle_w) {
            log_err ("flux_idle_watcher_create");
            return -1;
        }

        p->state_check_w = flux_check_watcher_create (p->reactor,
                                                      state_change_check_cb,
                                                      p);
        if (!p->state_check_w) {
            log_err ("flux_check_watcher_create");
            return -1;
        }
    }
    return 0;
}

static void completed_prep_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    flux_subprocess_t *p = arg;

    assert (p->completed);

    flux_watcher_start (p->completed_idle_w);
}

static void completed_check_cb (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    flux_subprocess_t *p = arg;

    assert (p->completed);

    flux_watcher_stop (p->completed_idle_w);

    /* always a chance caller may destroy subprocess in callback */
    subprocess_incref (p);

    /* There is a small "racy" component, where the state we're at may
     * not yet align with the state that has been reported to the
     * user.  We would like to report state EXITED to the user before
     * calling the completion callback.
     *
     * If no state change callback was specified, we must have reached
     * state FLUX_SUBPROCESS_EXITED to have reached this point.
     */
    if (!p->ops.on_state_change
        || p->state_reported == FLUX_SUBPROCESS_EXITED) {
        if (p->ops.on_completion)
            (*p->ops.on_completion) (p);

        flux_watcher_stop (p->completed_prep_w);
        flux_watcher_stop (p->completed_check_w);
    }

    subprocess_decref (p);
}

static int subprocess_setup_completed (flux_subprocess_t *p)
{
    if (p->ops.on_completion) {
        p->completed_prep_w = flux_prepare_watcher_create (p->reactor,
                                                           completed_prep_cb,
                                                           p);
        if (!p->completed_prep_w) {
            log_err ("flux_prepare_watcher_create");
            return -1;
        }

        p->completed_idle_w = flux_idle_watcher_create (p->reactor,
                                                        NULL,
                                                        p);
        if (!p->completed_idle_w) {
            log_err ("flux_idle_watcher_create");
            return -1;
        }

        p->completed_check_w = flux_check_watcher_create (p->reactor,
                                                          completed_check_cb,
                                                          p);
        if (!p->completed_check_w) {
            log_err ("flux_check_watcher_create");
            return -1;
        }

        /* start when process completed */
    }
    return 0;
}

flux_subprocess_t *flux_local_exec_ex (flux_reactor_t *r,
                                       int flags,
                                       const flux_cmd_t *cmd,
                                       const flux_subprocess_ops_t *ops,
                                       const flux_subprocess_hooks_t *hooks,
                                       subprocess_log_f log_fn,
                                       void *log_data)
{
    flux_subprocess_t *p = NULL;
    int valid_flags = (FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH
                       | FLUX_SUBPROCESS_FLAGS_NO_SETPGRP
                       | FLUX_SUBPROCESS_FLAGS_FORK_EXEC);

    if (!r || !cmd) {
        errno = EINVAL;
        return NULL;
    }

    if (flags & ~valid_flags) {
        errno = EINVAL;
        return NULL;
    }

    /* user required to set some args */
    if (!flux_cmd_argc (cmd)) {
        errno = EINVAL;
        goto error;
    }

    if (!(p = subprocess_create (NULL,
                                 r,
                                 flags,
                                 cmd,
                                 ops,
                                 hooks,
                                 -1,
                                 true,
                                 log_fn,
                                 log_data)))
        goto error;

    if (subprocess_local_setup (p) < 0)
        goto error;

    if (subprocess_setup_state_change (p) < 0)
        goto error;

    state_change_start (p);

    if (subprocess_setup_completed (p) < 0)
        goto error;

    return p;

error:
    subprocess_decref (p);
    return NULL;
}

flux_subprocess_t * flux_local_exec (flux_reactor_t *r,
                                     int flags,
                                     const flux_cmd_t *cmd,
                                     const flux_subprocess_ops_t *ops)
{
    return flux_local_exec_ex (r, flags, cmd, ops, NULL, NULL, NULL);
}

flux_future_t *flux_rexec_bg (flux_t *h,
                              const char *service_name,
                              int rank,
                              int flags,
                              const flux_cmd_t *cmd)
{
    if (!h
        || (rank < 0
            && rank != FLUX_NODEID_ANY
            && rank != FLUX_NODEID_UPSTREAM)
        || !cmd
        || !flux_cmd_argc (cmd)) {
        errno = EINVAL;
        return NULL;
    }
    return subprocess_rexec_bg (h, service_name, rank, cmd, flags);
}

flux_future_t *flux_rexec_wait (flux_t *h,
                                const char *service_name,
                                int rank,
                                pid_t pid,
                                const char *label)
{
    char *topic = NULL;
    flux_future_t *f = NULL;

    if (!h
        || (pid <= (pid_t) 0 && !label)   // neither pid nor label set
        || (pid > (pid_t) 0 && label)     // both specified: ambiguous
        || (rank < 0
            && rank != FLUX_NODEID_ANY
            && rank != FLUX_NODEID_UPSTREAM)) {
        errno = EINVAL;
        return NULL;
    }
    if (!service_name)
        service_name = "rexec";
    if (asprintf (&topic, "%s.wait", service_name) < 0)
        goto out;
    if (label)
        f = flux_rpc_pack (h,
                           topic,
                           rank,
                           0,
                           "{s:i s:s}",
                           "pid", -1,
                           "label", label);
    else
        f = flux_rpc_pack (h, topic, rank, 0, "{s:i}", "pid", pid);
out:
    ERRNO_SAFE_WRAP (free, topic);
    return f;
}


flux_subprocess_t *flux_rexec_ex (flux_t *h,
                                  const char *service_name,
                                  int rank,
                                  int flags,
                                  const flux_cmd_t *cmd,
                                  const flux_subprocess_ops_t *ops,
                                  subprocess_log_f log_fn,
                                  void *log_data)
{
    flux_subprocess_t *p = NULL;
    flux_reactor_t *r;
    int valid_flags = (FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH
                       | FLUX_SUBPROCESS_FLAGS_NO_SETPGRP
                       | FLUX_SUBPROCESS_FLAGS_FORK_EXEC
                       | FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF);

    if (!h
        || (rank < 0
            && rank != FLUX_NODEID_ANY
            && rank != FLUX_NODEID_UPSTREAM)
        || !cmd
        || !service_name) {
        errno = EINVAL;
        return NULL;
    }

    if (flags & ~valid_flags) {
        errno = EINVAL;
        return NULL;
    }

    /* user required to set some args */
    if (!flux_cmd_argc (cmd)) {
        errno = EINVAL;
        goto error;
    }

    if (!(r = flux_get_reactor (h)))
        goto error;

    if (!(p = subprocess_create (h,
                                 r,
                                 flags,
                                 cmd,
                                 ops,
                                 NULL,
                                 rank,
                                 false,
                                 log_fn,
                                 log_data)))
        goto error;

    if (subprocess_remote_setup (p, service_name) < 0)
        goto error;

    if (subprocess_setup_state_change (p) < 0)
        goto error;

    if (subprocess_setup_completed (p) < 0)
        goto error;

    if (remote_exec (p) < 0)
        goto error;

    return p;

error:
    subprocess_decref (p);
    return NULL;
}

flux_subprocess_t *flux_rexec (flux_t *h,
                               int rank,
                               int flags,
                               const flux_cmd_t *cmd,
                               const flux_subprocess_ops_t *ops)
{
    return flux_rexec_ex (h, "rexec", rank, flags, cmd, ops, NULL, NULL);
}

void flux_subprocess_stream_start (flux_subprocess_t *p, const char *stream)
{
    struct subprocess_channel *c;

    if (!p
        || !stream
        || !p->local
        || p->in_hook
        || !(c = zhash_lookup (p->channels, stream))
        || !(c->flags & CHANNEL_READ)
        || c->buffer_read_w_started
        || !fbuf_read_watcher_get_buffer (c->buffer_read_w))
        return;

    if (!c->buffer_read_stopped_w) {
        /* use check watcher instead of idle watcher, as idle watcher
         * could spin reactor */
        c->buffer_read_stopped_w = flux_check_watcher_create (p->reactor,
                                                              NULL,
                                                              c);
        if (!c->buffer_read_stopped_w)
            return;
    }

    /* Note that in local.c, we never stop buffer_read_w
     * (i.e. flux_watcher_stop (c->buffer_read_w)).  The watcher will
     * be stopped when the subprocess is destroyed.  So if by chance
     * the caller has already read EOF from this buffer, they can
     * re-start it without any harm.
     */
    flux_watcher_start (c->buffer_read_w);
    c->buffer_read_w_started = true;
    flux_watcher_stop (c->buffer_read_stopped_w);
}

void flux_subprocess_stream_stop (flux_subprocess_t *p, const char *stream)
{
    struct subprocess_channel *c;

    if (!p
        || !stream
        || !p->local
        || p->in_hook
        || !(c = zhash_lookup (p->channels, stream))
        || !(c->flags & CHANNEL_READ)
        || !c->buffer_read_w_started
        || !fbuf_read_watcher_get_buffer (c->buffer_read_w))
        return;

    flux_watcher_stop (c->buffer_read_w);
    c->buffer_read_w_started = false;
    flux_watcher_start (c->buffer_read_stopped_w);
}

int flux_subprocess_write (flux_subprocess_t *p,
                           const char *stream,
                           const char *buf,
                           size_t len)
{
    struct subprocess_channel *c;
    struct fbuf *fb;
    int ret;

    if (!p || !stream
           || (p->local && p->in_hook)) {
        errno = EINVAL;
        return -1;
    }

    if (!buf || !len) {
        errno = EINVAL;
        return -1;
    }

    c = zhash_lookup (p->channels, stream);
    if (!c || !(c->flags & CHANNEL_WRITE)) {
        errno = EINVAL;
        return -1;
    }

    if (c->closed) {
        errno = EPIPE;
        return -1;
    }

    if (p->local) {
        if (p->state != FLUX_SUBPROCESS_RUNNING) {
            errno = EPIPE;
            return -1;
        }
        if (!(fb = fbuf_write_watcher_get_buffer (c->buffer_write_w))) {
            log_err ("fbuf_write_watcher_get_buffer");
            return -1;
        }
        if (fbuf_space (fb) < len) {
            errno = ENOSPC;
            return -1;
        }
        if ((ret = fbuf_write (fb, buf, len)) < 0) {
            log_err ("fbuf_write");
            return -1;
        }
        c->buffer_space -= ret;
    }
    else {
        if (p->state != FLUX_SUBPROCESS_INIT
            && p->state != FLUX_SUBPROCESS_RUNNING) {
            errno = EPIPE;
            return -1;
        }
        if (subprocess_write (p->f, c->name, buf, len, false) < 0) {
            log_err ("error sending rexec.write request: %s",
                     strerror (errno));
            return -1;
        }
        ret = len;
    }

    return ret;
}

int flux_subprocess_close (flux_subprocess_t *p, const char *stream)
{
    struct subprocess_channel *c;

    if (!p || !stream
           || (p->local && p->in_hook)) {
        errno = EINVAL;
        return -1;
    }

    c = zhash_lookup (p->channels, stream);
    if (!c || !(c->flags & CHANNEL_WRITE)) {
        errno = EINVAL;
        return -1;
    }

    if (c->closed)
        return 0;

    if (p->local) {
        if (p->state == FLUX_SUBPROCESS_RUNNING) {
            if (fbuf_write_watcher_close (c->buffer_write_w) < 0) {
                log_err ("fbuf_write_watcher_close");
                return -1;
            }
        }
        /* else p->state == FLUX_SUBPROCESS_EXITED
           || p->state == FLUX_SUBPROCESS_FAILED
        */
        c->closed = true;
    }
    else {
        if (subprocess_write (p->f, c->name, NULL, 0, true) < 0) {
            log_err ("error sending rexec.write request: %s",
                     strerror (errno));
            return -1;
        }
        c->closed = true;
    }

    return 0;
}

static int subprocess_read (flux_subprocess_t *p,
                            const char *stream,
                            const char **bufp,
                            bool read_line,
                            bool trimmed,
                            bool line_buffered_required,
                            bool *readonly)
{
    struct subprocess_channel *c;
    struct fbuf *fb;
    const char *ptr;
    int len;

    if (!p || !stream
           || (p->local && p->in_hook)) {
        errno = EINVAL;
        return -1;
    }

    c = zhash_lookup (p->channels, stream);
    if (!c || !(c->flags & CHANNEL_READ)) {
        errno = EINVAL;
        return -1;
    }

    if (line_buffered_required && !c->line_buffered) {
        errno = EPERM;
        return -1;
    }

    if (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF) {
        if (bufp && c->unbuf_len)
            (*bufp) = c->unbuf_data;
        return c->unbuf_len;
    }

    if (p->local) {
        if (!(fb = fbuf_read_watcher_get_buffer (c->buffer_read_w)))
            return -1;
    }
    else
        fb = c->read_buffer;

    /* if readonly marked, indicates EOF received */
    if (readonly)
        (*readonly) = fbuf_is_readonly (fb);

    if (read_line) {
        if (trimmed) {
            if (!(ptr = fbuf_read_trimmed_line (fb, &len)))
                return -1;
        }
        else {
            if (!(ptr = fbuf_read_line (fb, &len)))
                return -1;
        }
        /* Special case if buffer full, we gotta flush it out even if
         * there is no line
         */
        if (!len && !fbuf_space (fb)) {
            if (!(ptr = fbuf_read (fb, -1, &len)))
                return -1;
        }
    }
    else {
        if (!(ptr = fbuf_read (fb, -1, &len)))
            return -1;
    }

    if (bufp && len > 0)
        (*bufp) = ptr;
    return len;
}

int flux_subprocess_read (flux_subprocess_t *p,
                          const char *stream,
                          const char **bufp)
{
    return subprocess_read (p, stream, bufp, false, false, false, NULL);
}


int flux_subprocess_read_line (flux_subprocess_t *p,
                               const char *stream,
                               const char **bufp)
{
    if (p && (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF)) {
        errno = EPERM;
        return -1;
    }
    return subprocess_read (p, stream, bufp, true, false, false, NULL);
}

int flux_subprocess_read_trimmed_line (flux_subprocess_t *p,
                                       const char *stream,
                                       const char **bufp)
{
    if (p && (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF)) {
        errno = EPERM;
        return -1;
    }
    return subprocess_read (p, stream, bufp, true, true, false, NULL);
}

bool flux_subprocess_read_stream_closed (flux_subprocess_t *p,
                                         const char *stream)
{
    struct subprocess_channel *c;
    struct fbuf *fb;

    if (!p
        || !stream
        || (p->local && p->in_hook)
        || !(c = zhash_lookup (p->channels, stream))
        || !(c->flags & CHANNEL_READ))
        return false;

    if (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF)
        return c->read_eof_received;

    if (p->local)
        fb = fbuf_read_watcher_get_buffer (c->buffer_read_w);
    else
        fb = c->read_buffer;

    return fb ? fbuf_is_readonly (fb) : false;
}

int flux_subprocess_getline (flux_subprocess_t *p,
                             const char *stream,
                             const char **bufp)
{
    int len;
    bool readonly;

    if (p && (p->flags & FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF)) {
        errno = EPERM;
        return -1;
    }

    len = subprocess_read (p, stream, bufp, true, false, true, &readonly);

    /* if no lines available and EOF received, read whatever is
     * lingering in the buffer */
    if (len == 0 && readonly)
        len = flux_subprocess_read (p, stream, bufp);

    return len;
}

static flux_future_t *add_pending_signal (flux_subprocess_t *p, int signum)
{
    flux_future_t *f;
    /*  There can only be one pending signal. Return an error if so.
     */
    if (p->signal_pending) {
        errno = EINVAL;
        return NULL;
    }
    if ((f = flux_future_create (NULL, NULL))) {
        flux_subprocess_aux_set (p, "sp::signal_future", f, NULL);
        p->signal_pending = signum;
        /*  Take a reference on the returned future in case the caller
         *  destroys it between now and when the signal is actually sent.
         *  This reference will be dropped in fwd_pending_signal()
         */
        flux_future_incref (f);
    }
    return f;
}

static bool subprocess_signal_allowed (flux_subprocess_t *p)
{
    /* Only allow a subprocess to be signaled if it is running or
     * initializing if there is no process group, otherwise allow
     * signaling any active subprocess.
     *
     * In the process group case, this ensures children of the original
     * subprocess that remain in the process group with stdio open can be
     * delivered a signal after the main pid exits (See issue #6712).
     */
    if ((p->flags & FLUX_SUBPROCESS_FLAGS_NO_SETPGRP)) {
        return p->state == FLUX_SUBPROCESS_RUNNING
                || p->state == FLUX_SUBPROCESS_INIT;
    }
    return flux_subprocess_active (p);
}

static flux_future_t *kill_create (pid_t pid, int sig)
{
    flux_future_t *f;
    flux_error_t error;
    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    if (kill (pid, sig) < 0) {
        errprintf (&error, "kill: %s", strerror (errno));
        flux_future_fulfill_error (f, errno, error.text);
    }
    else
        flux_future_fulfill (f, NULL, NULL);
    return f;
}

static flux_future_t *killpg_create (int pgrp, int sig)
{
    flux_future_t *f;
    flux_error_t error;
    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    if (killpg (pgrp, sig) < 0) {
        errprintf (&error, "killpg: %s", strerror (errno));
        flux_future_fulfill_error (f, errno, error.text);
    }
    else
        flux_future_fulfill (f, NULL, NULL);
    return f;
}

flux_future_t *flux_subprocess_kill (flux_subprocess_t *p, int signum)
{
    flux_future_t *f = NULL;

    if (!p || (p->local && p->in_hook)
        || !signum) {
        errno = EINVAL;
        return NULL;
    }

    if (!subprocess_signal_allowed (p)) {
        errno = ESRCH;
        return NULL;
    }

    if (p->local) {
        if (p->pid <= (pid_t) 0) {
            errno = ESRCH;
            return NULL;
        }
        if (!(p->flags & FLUX_SUBPROCESS_FLAGS_NO_SETPGRP))
            f = killpg_create (p->pid, signum);
        else
            f = kill_create (p->pid, signum);
    }
    else {
        if (p->state == FLUX_SUBPROCESS_INIT)
            f = add_pending_signal (p, signum);
        else
            f = remote_kill (p, signum);
    }
    /*  Future must have a reactor in order to call flux_future_then(3):
     */
    if (f && !flux_future_get_reactor (f))
        flux_future_set_reactor (f, p->reactor);
    return f;
}

void subprocess_incref (flux_subprocess_t *p)
{
    if (p) {
        if (p->local && p->in_hook)
            return;
        p->refcount++;
    }
}

void subprocess_decref (flux_subprocess_t *p)
{
    if (p) {
        if (p->local && p->in_hook)
            return;
        if (--p->refcount == 0)
            subprocess_free (p);
    }
}

void flux_subprocess_destroy (flux_subprocess_t *p)
{
    subprocess_decref (p);
}

flux_subprocess_state_t flux_subprocess_state (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    return p->state;
}

bool flux_subprocess_active (flux_subprocess_t *p)
{
    /*  A subprocess is still active if it has not failed or completed.
     */
    return (p && p->state != FLUX_SUBPROCESS_FAILED && !p->completed);
}

const char *flux_subprocess_state_string (flux_subprocess_state_t state)
{
    switch (state)
    {
    case FLUX_SUBPROCESS_INIT:
        return "Init";
    case FLUX_SUBPROCESS_RUNNING:
        return "Running";
    case FLUX_SUBPROCESS_EXITED:
        return "Exited";
    case FLUX_SUBPROCESS_FAILED:
        return "Failed";
    case FLUX_SUBPROCESS_STOPPED:
        return "Stopped";
    }
    return NULL;
}

int flux_subprocess_rank (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    if (p->local) {
        errno = EINVAL;
        return -1;
    }
    return p->rank;
}

int flux_subprocess_fail_errno (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    if (p->state != FLUX_SUBPROCESS_FAILED) {
        errno = EINVAL;
        return -1;
    }
    return p->failed_errno;
}

const char *flux_subprocess_fail_error (flux_subprocess_t *p)
{
    if (!p)
        return "internal error: subprocess is NULL";
    if (p->state != FLUX_SUBPROCESS_FAILED)
        return "internal error: subprocess is not in FAILED state";
    if (p->failed_error.text[0] == '\0')
        return strerror (p->failed_errno);
    return p->failed_error.text;
}

int flux_subprocess_status (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    if (p->state != FLUX_SUBPROCESS_EXITED) {
        errno = EINVAL;
        return -1;
    }
    return p->status;
}

int flux_subprocess_exit_code (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    if (p->state != FLUX_SUBPROCESS_EXITED) {
        errno = EINVAL;
        return -1;
    }
    if (!WIFEXITED (p->status)) {
        errno = EINVAL;
        return -1;
    }
    return WEXITSTATUS (p->status);
}

int flux_subprocess_signaled (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    if (p->state != FLUX_SUBPROCESS_EXITED) {
        errno = EINVAL;
        return -1;
    }
    if (!WIFSIGNALED (p->status)) {
        errno = EINVAL;
        return -1;
    }
    return WTERMSIG (p->status);
}

pid_t flux_subprocess_pid (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return -1;
    }
    /* do not return to user if the pid not yet received/set.  Note
     * that checking the state here isn't safe, b/c
     * FLUX_SUBPROCESS_FAILED may have occurred at any point before /
     * after the pid was available */
    if (!p->pid_set) {
        errno = EINVAL;
        return -1;
    }
    return p->pid;
}

flux_cmd_t * flux_subprocess_get_cmd (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return p->cmd;
}

flux_reactor_t * flux_subprocess_get_reactor (flux_subprocess_t *p)
{
    if (!p) {
        errno = EINVAL;
        return NULL;
    }
    return p->reactor;
}

int flux_subprocess_aux_set (flux_subprocess_t *p,
                             const char *name,
                             void *x,
                             flux_free_f free_fn)
{
    if (!p || (p->local && p->in_hook)) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&p->aux, name, x, free_fn);
}

void * flux_subprocess_aux_get (flux_subprocess_t *p, const char *name)
{
    if (!p || (p->local && p->in_hook)) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (p->aux, name);
}

int flux_set_default_subprocess_log (flux_t *h,
                                     subprocess_log_f log_fn,
                                     void *log_data)
{
    if (flux_aux_set (h, "flux::subprocess_llog_fn", log_fn, NULL) < 0
        || flux_aux_set (h, "flux::subprocess_llog_data", log_data, NULL) < 0)
        return -1;
    return 0;
}

void flux_subprocess_channel_incref (flux_subprocess_t *p, const char *name)
{
    struct subprocess_channel *c;
    if (!p || !p->local)
        return;
    if (!(c = zhash_lookup (p->channels, name)))
        return;
    fbuf_read_watcher_incref (c->buffer_read_w);
}

void flux_subprocess_channel_decref (flux_subprocess_t *p, const char *name)
{
    struct subprocess_channel *c;
    if (!p || !p->local)
        return;
    if (!(c = zhash_lookup (p->channels, name)))
        return;
    fbuf_read_watcher_decref (c->buffer_read_w);
}


/*
 * vi: ts=4 sw=4 expandtab
 */
