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
#include <wait.h>
#include <unistd.h>
#include <errno.h>

#include <czmq.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/aux.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "local.h"
#include "remote.h"
#include "server.h"
#include "util.h"

/*
 * Primary Structures
 */

void channel_destroy (void *arg)
{
    struct subprocess_channel *c = arg;
    if (c && c->magic == CHANNEL_MAGIC) {
        if (c->name)
            free (c->name);

        if (c->parent_fd != -1)
            close (c->parent_fd);
        if (c->child_fd != -1)
            close (c->child_fd);
        flux_watcher_destroy (c->buffer_write_w);
        flux_watcher_destroy (c->buffer_read_w);

        flux_buffer_destroy (c->write_buffer);
        flux_buffer_destroy (c->read_buffer);
        flux_watcher_destroy (c->in_prep_w);
        flux_watcher_destroy (c->in_idle_w);
        flux_watcher_destroy (c->in_check_w);
        flux_watcher_destroy (c->out_prep_w);
        flux_watcher_destroy (c->out_idle_w);
        flux_watcher_destroy (c->out_check_w);

        c->magic = ~CHANNEL_MAGIC;
        free (c);
    }
}

struct subprocess_channel *channel_create (flux_subprocess_t *p,
                                           flux_subprocess_output_f output_f,
                                           const char *name,
                                           int flags)
{
    struct subprocess_channel *c = calloc (1, sizeof (*c));
    int save_errno;

    if (!c)
        return NULL;

    c->magic = CHANNEL_MAGIC;

    c->p = p;
    c->output_f = output_f;
    if (!(c->name = strdup (name)))
        goto error;
    c->flags = flags;

    c->eof_sent_to_caller = false;
    c->closed = false;

    c->parent_fd = -1;
    c->child_fd = -1;
    c->buffer_write_w = NULL;
    c->buffer_read_w = NULL;

    c->write_buffer = NULL;
    c->read_buffer = NULL;
    c->write_eof_sent = false;
    c->read_eof_received = false;
    c->in_prep_w = NULL;
    c->in_idle_w = NULL;
    c->in_check_w = NULL;
    c->out_prep_w = NULL;
    c->out_idle_w = NULL;
    c->out_check_w = NULL;

    return c;

error:
    save_errno = errno;
    channel_destroy (c);
    errno = save_errno;
    return NULL;
}

static void subprocess_free (flux_subprocess_t *p)
{
    if (p && p->magic == SUBPROCESS_MAGIC) {
        flux_cmd_destroy (p->cmd);

        aux_destroy (&p->aux);
        if (p->channels)
            zhash_destroy (&p->channels);

        flux_watcher_destroy (p->child_w);

        close_pair_fds (p->sync_fds);

        flux_watcher_destroy (p->state_prep_w);
        flux_watcher_destroy (p->state_idle_w);
        flux_watcher_destroy (p->state_check_w);

        flux_watcher_destroy (p->completed_prep_w);
        flux_watcher_destroy (p->completed_idle_w);
        flux_watcher_destroy (p->completed_check_w);

        if (p->f)
            flux_future_destroy (p->f);

        p->magic = ~SUBPROCESS_MAGIC;
        free (p);
    }
}

static flux_subprocess_t * subprocess_create (flux_t *h,
                                              flux_reactor_t *r,
                                              int flags,
                                              const flux_cmd_t *cmd,
                                              flux_subprocess_ops_t *ops,
                                              int rank,
                                              bool local)
{
    flux_subprocess_t *p = calloc (1, sizeof (*p));
    int save_errno;

    if (!p)
        return NULL;

    p->magic = SUBPROCESS_MAGIC;

    /* init fds, so on error we don't accidentally close stdin
     * (i.e. fd == 0)
     */
    init_pair_fds (p->sync_fds);

    /* set CLOEXEC on sync_fds, so on exec(), child sync_fd is closed
     * and seen by parent */
    if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, p->sync_fds) < 0)
        goto error;

    if (!(p->channels = zhash_new ()))
        goto error;

    p->state = FLUX_SUBPROCESS_INIT;
    p->state_reported = p->state;

    if (!(p->cmd = flux_cmd_copy (cmd)))
        goto error;

    if (ops)
        p->ops = *ops;

    p->h = h;
    p->reactor = r;
    p->rank = rank;
    p->flags = flags;

    p->local = local;

    p->refcount = 1;
    return (p);

error:
    save_errno = errno;
    subprocess_free (p);
    errno = save_errno;
    return NULL;
}

static void subprocess_server_destroy (void *arg)
{
    flux_subprocess_server_t *s = arg;
    if (s && s->magic == SUBPROCESS_SERVER_MAGIC) {
        /* s->handlers handled in server_stop, this is for destroying
         * things only
         */
        zhash_destroy (&s->subprocesses);
        free (s->local_uri);
        s->magic = ~SUBPROCESS_SERVER_MAGIC;
        free (s);
    }
}

static flux_subprocess_server_t *subprocess_server_create (flux_t *h,
                                                           const char *local_uri,
                                                           int rank)
{
    flux_subprocess_server_t *s = calloc (1, sizeof (*s));
    int save_errno;

    if (!s)
        return NULL;

    s->magic = SUBPROCESS_SERVER_MAGIC;
    s->h = h;
    if (!(s->r = flux_get_reactor (h)))
        goto error;
    if (!(s->subprocesses = zhash_new ()))
        goto error;
    if (!(s->local_uri = strdup (local_uri)))
        goto error;
    s->rank = rank;

    return s;

error:
    save_errno = errno;
    subprocess_server_destroy (s);
    errno = save_errno;
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

/*
 *  General support:
 */

flux_subprocess_server_t *flux_subprocess_server_start (flux_t *h,
                                                        const char *prefix,
                                                        const char *local_uri,
                                                        uint32_t rank)
{
    flux_subprocess_server_t *s = NULL;
    int save_errno;

    if (!h || !prefix || !local_uri) {
        errno = EINVAL;
        goto error;
    }

    if (!(s = subprocess_server_create (h, local_uri, rank)))
        goto error;

    if (server_start (s, prefix) < 0)
        goto error;

    return s;

error:
    save_errno = errno;
    subprocess_server_destroy (s);
    errno = save_errno;
    return NULL;
}

void flux_subprocess_server_stop (flux_subprocess_server_t *s)
{
    if (s && s->magic == SUBPROCESS_SERVER_MAGIC) {
        server_stop (s);
        server_terminate_subprocesses (s);
        subprocess_server_destroy (s);
    }
}

int flux_subprocess_server_terminate_by_uuid (flux_subprocess_server_t *s,
                                              const char *id)
{
    if (!s || s->magic != SUBPROCESS_SERVER_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    return server_terminate_by_uuid (s, id);
}

/*
 * Convenience Functions:
 */

void flux_standard_output (flux_subprocess_t *p, const char *stream)
{
    /* everything except stderr goes to stdout */
    FILE *fstream = !strcasecmp (stream, "STDERR") ? stderr : stdout;
    const char *ptr;
    int lenp;

    if (!(ptr = flux_subprocess_read_line (p, stream, &lenp))) {
        log_err ("flux_standard_output: read_line");
        return;
    }

    /* if process exited, read remaining stuff or EOF, otherwise
     * wait for future newline */
    if (!lenp
        && flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED) {
        if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
            log_err ("flux_standard_output: read_line");
            return;
        }
    }

    if (lenp)
        fwrite (ptr, lenp, 1, fstream);
}

/*
 *  Process handling:
 */

void subprocess_check_completed (flux_subprocess_t *p)
{
    assert (p->state == FLUX_SUBPROCESS_EXITED);

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
    assert (p->state != FLUX_SUBPROCESS_FAILED);

    switch (p->state_reported) {
    case FLUX_SUBPROCESS_INIT:
        /* next state to report must be STARTED */
        return FLUX_SUBPROCESS_STARTED;
    case FLUX_SUBPROCESS_STARTED:
        /* next state must be RUNNING or EXEC_FAILED */
        if (p->state == FLUX_SUBPROCESS_EXEC_FAILED)
            return FLUX_SUBPROCESS_EXEC_FAILED;
        else /* p->state == FLUX_SUBPROCESS_RUNNING
                || p->state == FLUX_SUBPROCESS_EXITED */
            return FLUX_SUBPROCESS_RUNNING;
    case FLUX_SUBPROCESS_RUNNING:
        /* next state is EXITED */
        return FLUX_SUBPROCESS_EXITED;
    case FLUX_SUBPROCESS_EXEC_FAILED:
    case FLUX_SUBPROCESS_EXITED:
    case FLUX_SUBPROCESS_FAILED:
        break;
    }

    /* shouldn't be possible to reach here */
    assert (0);
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
    flux_subprocess_ref (p);

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
    if (p->state_reported == FLUX_SUBPROCESS_EXEC_FAILED
        || p->state_reported == FLUX_SUBPROCESS_EXITED
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

    flux_subprocess_unref (p);
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
    flux_subprocess_ref (p);

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

    flux_subprocess_unref (p);
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

static flux_subprocess_t * flux_exec_wrap (flux_t *h, flux_reactor_t *r, int flags,
                                           const flux_cmd_t *cmd,
                                           flux_subprocess_ops_t *ops)
{
    flux_subprocess_t *p = NULL;
    int valid_flags = (FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH
                       | FLUX_SUBPROCESS_FLAGS_SETPGRP);
    int save_errno;

    if (!r || !cmd) {
        errno = EINVAL;
        return NULL;
    }

    if (flags & ~valid_flags) {
        errno = EINVAL;
        return NULL;
    }

    if (!(p = subprocess_create (h, r, flags, cmd, ops, -1, true)))
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
    save_errno = errno;
    flux_subprocess_unref (p);
    errno = save_errno;
    return NULL;
}

flux_subprocess_t * flux_exec (flux_t *h, int flags,
                               const flux_cmd_t *cmd,
                               flux_subprocess_ops_t *ops)
{
    flux_reactor_t *r;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }

    if (!(r = flux_get_reactor (h)))
        return NULL;

    return flux_exec_wrap (h, r, flags, cmd, ops);
}

flux_subprocess_t * flux_local_exec (flux_reactor_t *r, int flags,
                                     const flux_cmd_t *cmd,
                                     flux_subprocess_ops_t *ops)
{
    return flux_exec_wrap (NULL, r, flags, cmd, ops);
}

flux_subprocess_t *flux_rexec (flux_t *h, int rank, int flags,
                               const flux_cmd_t *cmd,
                               flux_subprocess_ops_t *ops)
{
    flux_subprocess_t *p = NULL;
    flux_reactor_t *r;
    int save_errno;

    if (!h
        || (rank < 0
            && rank != FLUX_NODEID_ANY
            && rank != FLUX_NODEID_UPSTREAM)
        || !cmd) {
        errno = EINVAL;
        return NULL;
    }

    /* no flags supported yet */
    if (flags) {
        errno = EINVAL;
        return NULL;
    }

    /* user required to set some args */
    if (!flux_cmd_argc (cmd)) {
        errno = EINVAL;
        goto error;
    }

    /* user required to set cwd */
    if (!flux_cmd_getcwd (cmd)) {
        errno = EINVAL;
        goto error;
    }

    if (!(r = flux_get_reactor (h)))
        goto error;

    if (!(p = subprocess_create (h, r, flags, cmd, ops, rank, false)))
        goto error;

    if (subprocess_remote_setup (p) < 0)
        goto error;

    if (subprocess_setup_state_change (p) < 0)
        goto error;

    if (subprocess_setup_completed (p) < 0)
        goto error;

    if (remote_exec (p) < 0)
        goto error;

    return p;

error:
    save_errno = errno;
    flux_subprocess_unref (p);
    errno = save_errno;
    return NULL;
}

int flux_subprocess_write (flux_subprocess_t *p, const char *stream,
                           const char *buf, size_t len)
{
    struct subprocess_channel *c;
    flux_buffer_t *fb;
    int ret;

    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if (!buf || !len) {
        errno = EINVAL;
        return -1;
    }

    if (!stream)
        stream = "STDIN";

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
        if (p->state != FLUX_SUBPROCESS_STARTED
            && p->state != FLUX_SUBPROCESS_RUNNING) {
            errno = EPIPE;
            return -1;
        }
        if (!(fb = flux_buffer_write_watcher_get_buffer (c->buffer_write_w))) {
            log_err ("flux_buffer_write_watcher_get_buffer");
            return -1;
        }

        if ((ret = flux_buffer_write (fb, buf, len)) < 0) {
            log_err ("flux_buffer_write");
            return -1;
        }
    }
    else {
        if (p->state != FLUX_SUBPROCESS_INIT
            && p->state != FLUX_SUBPROCESS_STARTED
            && p->state != FLUX_SUBPROCESS_RUNNING) {
            errno = EPIPE;
            return -1;
        }
        if ((ret = flux_buffer_write (c->write_buffer, buf, len)) < 0) {
            log_err ("flux_buffer_write");
            return -1;
        }
    }

    return ret;
}

int flux_subprocess_close (flux_subprocess_t *p, const char *stream)
{
    struct subprocess_channel *c;

    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    if (!stream)
        stream = "STDIN";

    c = zhash_lookup (p->channels, stream);
    if (!c || !(c->flags & CHANNEL_WRITE)) {
        errno = EINVAL;
        return -1;
    }

    if (c->closed)
        return 0;

    if (p->local) {
        if (p->state == FLUX_SUBPROCESS_STARTED
            || p->state == FLUX_SUBPROCESS_RUNNING) {
            if (flux_buffer_write_watcher_close (c->buffer_write_w) < 0) {
                log_err ("flux_buffer_write_watcher_close");
                return -1;
            }
        }
        /* else p->state == FLUX_SUBPROCESS_EXEC_FAILED
           || p->state == FLUX_SUBPROCESS_EXITED
           || p->state == FLUX_SUBPROCESS_FAILED
        */
        c->closed = true;
    }
    else {
        /* doesn't matter about state, b/c reactors will send closed.
         * If those reactors are already turned off, it's b/c
         * subprocess failed/exited.
         */
        c->closed = true;
    }

    return 0;
}

static const char *subprocess_read (flux_subprocess_t *p,
                                    const char *stream,
                                    int len, int *lenp,
                                    bool read_line,
                                    bool trimmed)
{
    struct subprocess_channel *c;
    flux_buffer_t *fb;
    const char *ptr;

    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return NULL;
    }

    if (!read_line && len == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!stream)
        stream = "STDOUT";

    c = zhash_lookup (p->channels, stream);
    if (!c || !(c->flags & CHANNEL_READ)) {
        errno = EINVAL;
        return NULL;
    }

    if (p->local) {
        if (!(fb = flux_buffer_read_watcher_get_buffer (c->buffer_read_w)))
            return NULL;
    }
    else
        fb = c->read_buffer;

    if (read_line) {
        if (trimmed) {
            if (!(ptr = flux_buffer_read_trimmed_line (fb, lenp)))
                return NULL;
        }
        else {
            if (!(ptr = flux_buffer_read_line (fb, lenp)))
                return NULL;
        }
    }
    else {
        if (!(ptr = flux_buffer_read (fb, len, lenp)))
            return NULL;
    }

    return ptr;
}

const char *flux_subprocess_read (flux_subprocess_t *p,
                                  const char *stream,
                                  int len, int *lenp)
{
    return subprocess_read (p, stream, len, lenp, false, false);
}

const char *flux_subprocess_read_line (flux_subprocess_t *p,
                                       const char *stream,
                                       int *lenp)
{
    return subprocess_read (p, stream, 0, lenp, true, false);
}

const char *flux_subprocess_read_trimmed_line (flux_subprocess_t *p,
                                               const char *stream,
                                               int *lenp)
{
    return subprocess_read (p, stream, 0, lenp, true, true);
}

flux_future_t *flux_subprocess_kill (flux_subprocess_t *p, int signum)
{
    flux_future_t *f = NULL;

    if (!p || p->magic != SUBPROCESS_MAGIC || !signum) {
        errno = EINVAL;
        return NULL;
    }

    if (p->state != FLUX_SUBPROCESS_RUNNING) {
        /* XXX right errno? */
        errno = EINVAL;
        return NULL;
    }

    if (p->local) {
        int ret;
        if (p->flags & FLUX_SUBPROCESS_FLAGS_SETPGRP)
            ret = killpg (p->pid, signum);
        else
            ret = kill (p->pid, signum);
        f = flux_future_create (NULL, NULL);
        if (ret < 0)
            flux_future_fulfill_error (f, errno, NULL);
        else
            flux_future_fulfill (f, NULL, NULL);
    }
    else {
        if (!(f = remote_kill (p, signum))) {
            int save_errno = errno;
            f = flux_future_create (NULL, NULL);
            flux_future_fulfill_error (f, save_errno, NULL);
        }
    }
    return f;
}

void flux_subprocess_ref (flux_subprocess_t *p)
{
    if (p && p->magic == SUBPROCESS_MAGIC)
        p->refcount++;
}

void flux_subprocess_unref (flux_subprocess_t *p)
{
    if (p && p->magic == SUBPROCESS_MAGIC) {
        if (--p->refcount == 0)
            subprocess_free (p);
    }
}

flux_subprocess_state_t flux_subprocess_state (flux_subprocess_t *p)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    return p->state;
}

const char *flux_subprocess_state_string (flux_subprocess_state_t state)
{
    switch (state)
    {
    case FLUX_SUBPROCESS_INIT:
        return "Init";
    case FLUX_SUBPROCESS_STARTED:
        return "Started";
    case FLUX_SUBPROCESS_EXEC_FAILED:
        return "Exec Failed";
    case FLUX_SUBPROCESS_RUNNING:
        return "Running";
    case FLUX_SUBPROCESS_EXITED:
        return "Exited";
    case FLUX_SUBPROCESS_FAILED:
        return "Failed";
    }
    return NULL;
}

int flux_subprocess_rank (flux_subprocess_t *p)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
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
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    if (p->state != FLUX_SUBPROCESS_EXEC_FAILED
        && p->state != FLUX_SUBPROCESS_FAILED) {
        errno = EINVAL;
        return -1;
    }
    if (p->state == FLUX_SUBPROCESS_EXEC_FAILED)
        return p->exec_failed_errno;
    else
        return p->failed_errno;
}

int flux_subprocess_status (flux_subprocess_t *p)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
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
    if (!p || p->magic != SUBPROCESS_MAGIC) {
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
    if (!p || p->magic != SUBPROCESS_MAGIC) {
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
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    return p->pid;
}

flux_cmd_t * flux_subprocess_get_cmd (flux_subprocess_t *p)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return NULL;
    }
    return p->cmd;
}

flux_reactor_t * flux_subprocess_get_reactor (flux_subprocess_t *p)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return NULL;
    }
    return p->reactor;
}

int flux_subprocess_aux_set (flux_subprocess_t *p,
                             const char *name, void *x, flux_free_f free_fn)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&p->aux, name, x, free_fn);
}

void * flux_subprocess_aux_get (flux_subprocess_t *p, const char *name)
{
    if (!p || p->magic != SUBPROCESS_MAGIC) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (p->aux, name);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
