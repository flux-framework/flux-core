
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
#include "config.h"
#endif

#include <sys/types.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/macros.h"
#include "src/common/libioencode/ioencode.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "remote.h"
#include "server.h"
#include "util.h"

static const char *auxkey = "flux::rexec";

struct rexec {
    const flux_msg_t *msg;        // rexec request message
    flux_subprocess_server_t *s;  // server context
};

static void rexec_destroy (struct rexec *rex)
{
    if (rex) {
        flux_msg_decref (rex->msg);
        ERRNO_SAFE_WRAP (free, rex);
    }
}

static struct rexec *rexec_create (const flux_msg_t *msg, flux_subprocess_server_t *s)
{
    struct rexec *rex;

    if ((rex = calloc (1, sizeof (*rex)))) {
        rex->msg = flux_msg_incref (msg);
        rex->s = s;
    }
    return rex;
}

static void subprocesses_free_fn (void *arg)
{
    flux_subprocess_t *p = arg;

    flux_subprocess_unref (p);
}

static int store_pid (flux_subprocess_server_t *s, flux_subprocess_t *p)
{
    pid_t pid = flux_subprocess_pid (p);
    char *str = NULL;
    int rv = -1;
    void *ret = NULL;

    if (asprintf (&str, "%d", pid) < 0) {
        flux_log_error (s->h, "%s: asprintf", __FUNCTION__);
        goto cleanup;
    }

    if (zhash_insert (s->subprocesses, str, p) < 0) {
        flux_log_error (s->h, "%s: zhash_insert", __FUNCTION__);
        goto cleanup;
    }

    ret = zhash_freefn (s->subprocesses, str, subprocesses_free_fn);
    assert (ret);

    rv = 0;
cleanup:
    free (str);
    return rv;
}

static void remove_pid (flux_subprocess_server_t *s, flux_subprocess_t *p)
{
    pid_t pid = flux_subprocess_pid (p);
    char *str = NULL;

    if (asprintf (&str, "%d", pid) < 0) {
        flux_log_error (s->h, "%s: asprintf", __FUNCTION__);
        goto cleanup;
    }

    zhash_delete (s->subprocesses, str);

    if (!zhash_size (s->subprocesses) && s->terminate_prep_w) {
        flux_watcher_start (s->terminate_prep_w);
        flux_watcher_start (s->terminate_check_w);
    }

cleanup:
    free (str);
}

static flux_subprocess_t *lookup_pid (flux_subprocess_server_t *s, pid_t pid)
{
    flux_subprocess_t *p = NULL;
    char *str = NULL;
    int save_errno;

    if (asprintf (&str, "%d", pid) < 0)
        goto cleanup;

    if (!(p = zhash_lookup (s->subprocesses, str))) {
        errno = ENOENT;
        goto cleanup;
    }

cleanup:
    save_errno = errno;
    free (str);
    errno = save_errno;
    return p;
}

static void subprocess_cleanup (flux_subprocess_t *p)
{
    struct rexec *rex = flux_subprocess_aux_get (p, auxkey);

    assert (rex != NULL);

    remove_pid (rex->s, p);
}

static void rexec_completion_cb (flux_subprocess_t *p)
{
    struct rexec *rex = flux_subprocess_aux_get (p, auxkey);

    assert (rex != NULL);

    if (p->state != FLUX_SUBPROCESS_FAILED) {
        /* no fallback if this fails */
        if (flux_respond_pack (rex->s->h, rex->msg, "{s:s s:i}",
                               "type", "complete",
                               "rank", rex->s->rank) < 0)
            flux_log_error (rex->s->h, "%s: flux_respond_pack", __FUNCTION__);
    }

    subprocess_cleanup (p);
}

static void internal_fatal (flux_subprocess_server_t *s, flux_subprocess_t *p)
{
    if (p->state == FLUX_SUBPROCESS_FAILED)
        return;

    /* report of state change handled through typical state change
     * callback.  Normally cleanup occurs through completion of local
     * subprocess.
     */
    p->state = FLUX_SUBPROCESS_FAILED;
    p->failed_errno = errno;
    state_change_start (p);

    /* if we fail here, probably not much can be done */
    if (killpg (p->pid, SIGKILL) < 0) {
        if (errno != ESRCH)
            flux_log_error (s->h, "%s: kill", __FUNCTION__);
    }
}

static void rexec_state_change_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    struct rexec *rex = flux_subprocess_aux_get (p, auxkey);

    assert (rex != NULL);

    if (state == FLUX_SUBPROCESS_RUNNING) {
        if (store_pid (rex->s, p) < 0)
            goto error;
        if (flux_respond_pack (rex->s->h, rex->msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", rex->s->rank,
                               "pid", flux_subprocess_pid (p),
                               "state", state) < 0) {
            flux_log_error (rex->s->h, "%s: flux_respond_pack", __FUNCTION__);
        }
    }
    else if (state == FLUX_SUBPROCESS_EXITED) {
        if (flux_respond_pack (rex->s->h, rex->msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", rex->s->rank,
                               "state", state,
                               "status", flux_subprocess_status (p)) < 0) {
            flux_log_error (rex->s->h, "%s: flux_respond_pack", __FUNCTION__);
        }
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        if (flux_respond_pack (rex->s->h, rex->msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", rex->s->rank,
                               "state", FLUX_SUBPROCESS_FAILED,
                               "errno", p->failed_errno) < 0) {
            flux_log_error (rex->s->h, "%s: flux_respond_pack", __FUNCTION__);
        }
        subprocess_cleanup (p);
    }
    else {
        errno = EPROTO;
        flux_log_error (rex->s->h, "%s: illegal state", __FUNCTION__);
        goto error;
    }

    return;

error:
    internal_fatal (rex->s, p);
}

static int rexec_output (flux_subprocess_t *p,
                         const char *stream,
                         flux_subprocess_server_t *s,
                         const flux_msg_t *msg,
                         const char *data,
                         int len,
                         bool eof)
{
    json_t *io = NULL;
    char rankstr[64];
    int rv = -1;

    snprintf (rankstr, sizeof (rankstr), "%d", s->rank);
    if (!(io = ioencode (stream, rankstr, data, len, eof))) {
        flux_log_error (s->h, "%s: ioencode", __FUNCTION__);
        goto error;
    }

    if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:O}",
                           "type", "output",
                           "rank", s->rank,
                           "pid", flux_subprocess_pid (p),
                           "io", io) < 0) {
        flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    rv = 0;
error:
    json_decref (io);
    return rv;
}

static void rexec_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct rexec *rex = flux_subprocess_aux_get (p, auxkey);
    const char *ptr;
    int lenp;

    assert (rex != NULL);

    if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
        flux_log_error (rex->s->h, "%s: flux_subprocess_read", __FUNCTION__);
        goto error;
    }

    if (lenp) {
        if (rexec_output (p, stream, rex->s, rex->msg, ptr, lenp, false) < 0)
            goto error;
    }
    else {
        if (rexec_output (p, stream, rex->s, rex->msg, NULL, 0, true) < 0)
            goto error;
    }

    return;

error:
    internal_fatal (rex->s, p);
}

static void server_exec_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    flux_subprocess_server_t *s = arg;
    const char *cmd_str;
    flux_cmd_t *cmd = NULL;
    struct rexec *rex;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = rexec_completion_cb,
        .on_state_change = rexec_state_change_cb,
        .on_channel_out = rexec_output_cb,
        .on_stdout = rexec_output_cb,
        .on_stderr = rexec_output_cb,
    };
    int on_channel_out, on_stdout, on_stderr;
    char **env = NULL;

    if (s->auth_cb && (*s->auth_cb) (msg, s->arg) < 0)
        goto error;

    if (flux_request_unpack (msg, NULL, "{s:s s:i s:i s:i}",
                             "cmd", &cmd_str,
                             "on_channel_out", &on_channel_out,
                             "on_stdout", &on_stdout,
                             "on_stderr", &on_stderr))
        goto error;

    if (!on_channel_out)
        ops.on_channel_out = NULL;
    if (!on_stdout)
        ops.on_stdout = NULL;
    if (!on_stderr)
        ops.on_stderr = NULL;

    if (!(cmd = flux_cmd_fromjson (cmd_str, NULL)))
        goto error;

    if (!flux_cmd_argc (cmd)) {
        errno = EPROTO;
        goto error;
    }

    if (!(env = flux_cmd_env_expand (cmd)))
        goto error;

    /* if no environment sent, use local server environment */
    if (env[0] == NULL) {
        if (flux_cmd_set_env (cmd, environ) < 0) {
            flux_log_error (s->h, "%s: flux_cmd_set_env", __FUNCTION__);
            goto error;
        }
    }

    if (flux_cmd_setenvf (cmd, 1, "FLUX_URI", "%s", s->local_uri) < 0)
        goto error;

    if (flux_respond_pack (s->h, msg, "{s:s s:i}",
                           "type", "start",
                           "rank", s->rank) < 0) {
        flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    if (!(p = flux_exec (s->h, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops, NULL))) {
        /* error here, generate FLUX_SUBPROCESS_EXEC_FAILED state */
        if (flux_respond_pack (h, msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", s->rank,
                               "state", FLUX_SUBPROCESS_EXEC_FAILED,
                               "errno", errno) < 0) {
            flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
        goto cleanup;
    }

    if (!(rex = rexec_create (msg, s)))
        goto error;
    if (flux_subprocess_aux_set (p, auxkey, rex, (flux_free_f)rexec_destroy) < 0) {
        rexec_destroy (rex);
        goto error;
    }

    flux_cmd_destroy (cmd);
    free (env);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
cleanup:
    flux_cmd_destroy (cmd);
    free (env);
    flux_subprocess_unref (p);
}

static int write_subprocess (flux_subprocess_server_t *s,
                             flux_subprocess_t *p,
                             const char *stream,
                             const char *data,
                             int len)
{
    int tmp;

    if ((tmp = flux_subprocess_write (p, stream, data, len)) < 0) {
        flux_log_error (s->h, "%s: flux_subprocess_write", __FUNCTION__);
        return -1;
    }

    /* add list of msgs if there is overflow? */

    if (tmp != len) {
        flux_log_error (s->h,
                        "channel buffer error: "
                        "rank = %d pid = %d, stream = %s, len = %d",
                        s->rank,
                        flux_subprocess_pid (p),
                        stream,
                        len);
        errno = EOVERFLOW;
        return -1;
    }

    return 0;
}

static int close_subprocess (flux_subprocess_server_t *s,
                             flux_subprocess_t *p,
                             const char *stream)
{
    if (flux_subprocess_close (p, stream) < 0) {
        flux_log_error (s->h, "%s: flux_subprocess_close", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void server_write_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    flux_subprocess_t *p;
    flux_subprocess_server_t *s = arg;
    const char *stream = NULL;
    char *data = NULL;
    int len = 0;
    bool eof = false;
    pid_t pid;
    json_t *io = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:i s:o }",
                             "pid", &pid,
                             "io", &io) < 0) {
        /* can't handle error, no pid to sent errno back to, so just
         * return */
        flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }

    if (iodecode (io, &stream, NULL, &data, &len, &eof) < 0) {
        flux_log_error (s->h, "%s: iodecode", __FUNCTION__);
        return;
    }

    if (!(p = lookup_pid (s, pid))) {
        /* can't handle error, no pid to send errno back to, so just
         * return
         *
         * It's common on EOF to be sent and server has already
         * removed process from hash.  Don't output error in that
         * case.
         */
        if (!(errno == ENOENT && eof))
            flux_log_error (s->h, "%s: lookup_pid", __FUNCTION__);
        goto out;
    }

    /* Chance subprocess exited/killed/etc. since user write request
     * was sent.
     */
    if (p->state != FLUX_SUBPROCESS_RUNNING)
        goto out;

    if (data && len) {
        if (write_subprocess (s, p, stream, data, len) < 0)
            goto error;
    }
    if (eof) {
        if (close_subprocess (s, p, stream) < 0)
            goto error;
    }

out:
    free (data);
    return;

error:
    free (data);
    internal_fatal (s, p);
}

static void server_signal_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    flux_subprocess_server_t *s = arg;
    pid_t pid;
    int signum;

    errno = 0;

    if (flux_request_unpack (msg, NULL, "{ s:i s:i }",
                             "pid", &pid,
                             "signum", &signum) < 0) {
        flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
        errno = EPROTO;
        goto error;
    }

    if (!lookup_pid (s, pid))
        goto error;

    if (killpg (pid, signum) < 0) {
        flux_log_error (s->h, "kill");
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

const char *subprocess_sender (flux_subprocess_t *p)
{
    struct rexec *rex = flux_subprocess_aux_get (p, auxkey);
    const char *sender;

    if (!rex || !(sender = flux_msg_route_first (rex->msg)))
        return NULL;

    return sender;
}

static json_t *process_info (flux_subprocess_t *p)
{
    flux_cmd_t *cmd;
    char *cmd_str = NULL;
    const char *sender;
    json_t *info = NULL;

    if (!(cmd = flux_subprocess_get_cmd (p)))
        goto cleanup;

    if (!(cmd_str = flux_cmd_tojson (cmd)))
        goto cleanup;

    if (!(sender = subprocess_sender (p))) {
        errno = ENOENT;
        goto cleanup;
    }

    /* very limited returned, just for testing */
    if (!(info = json_pack ("{s:i s:s}",
                            "pid", flux_subprocess_pid (p),
                            "sender", sender))) {
        errno = ENOMEM;
        goto cleanup;
    }

cleanup:
    free (cmd_str);
    return info;
}

static void server_processes_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    flux_subprocess_server_t *s = arg;
    flux_subprocess_t *p;
    json_t *procs = NULL;

    if (!(procs = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    p = zhash_first (s->subprocesses);
    while (p) {
        json_t *o = NULL;
        if (!(o = process_info (p)) || json_array_append_new (procs, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }
        p = zhash_next (s->subprocesses);
    }

    if (flux_respond_pack (h, msg, "{s:i s:o}", "rank", s->rank,
                           "procs", procs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (procs);
}

int server_start (flux_subprocess_server_t *s)
{
    /* rexec.processes is primarily for testing */
    struct flux_msg_handler_spec htab[] = {
        {FLUX_MSGTYPE_REQUEST, "broker.rexec", server_exec_cb, 0},
        {FLUX_MSGTYPE_REQUEST, "broker.rexec.write", server_write_cb, 0},
        {FLUX_MSGTYPE_REQUEST, "broker.rexec.signal", server_signal_cb, 0},
        {FLUX_MSGTYPE_REQUEST, "broker.rexec.processes", server_processes_cb, 0},
        FLUX_MSGHANDLER_TABLE_END,
    };

    if (flux_msg_handler_addvec (s->h, htab, s, &s->handlers) < 0)
        return -1;

    return 0;
}

void server_stop (flux_subprocess_server_t *s)
{
    flux_msg_handler_delvec (s->handlers);
}

static void server_signal_subprocess (flux_subprocess_t *p, int signum)
{
    flux_future_t *f;
    if (!(f = flux_subprocess_kill (p, signum))) {
        struct rexec *rex = flux_subprocess_aux_get (p, auxkey);

        flux_log_error (rex->s->h, "%s: flux_subprocess_kill", __FUNCTION__);
        return;
    }
    flux_future_destroy (f);
}

int server_signal_subprocesses (flux_subprocess_server_t *s, int signum)
{
    flux_subprocess_t *p;

    p = zhash_first (s->subprocesses);
    while (p) {
        server_signal_subprocess (p, signum);
        p = zhash_next (s->subprocesses);
    }

    return 0;
}

int server_terminate_subprocesses (flux_subprocess_server_t *s)
{
    server_signal_subprocesses (s, SIGKILL);
    return 0;
}

static void terminate_uuid (flux_subprocess_t *p, const char *id)
{
    const char *sender;

    if (!(sender = subprocess_sender (p)))
        return;

    if (!strcmp (id, sender))
        server_signal_subprocess (p, SIGKILL);
}

int server_terminate_by_uuid (flux_subprocess_server_t *s, const char *id)
{
    flux_subprocess_t *p;

    p = zhash_first (s->subprocesses);
    while (p) {
        terminate_uuid (p, id);
        p = zhash_next (s->subprocesses);
    }

    return 0;
}

static void terminate_prep_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    flux_subprocess_server_t *s = arg;
    flux_watcher_start (s->terminate_idle_w);
}

static void terminate_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    flux_subprocess_server_t *s = arg;
    flux_watcher_stop (s->terminate_timer_w);
    flux_watcher_stop (s->terminate_prep_w);
    flux_watcher_stop (s->terminate_idle_w);
    flux_watcher_stop (s->terminate_check_w);
    flux_reactor_stop (s->r);
}

void server_terminate_cleanup (flux_subprocess_server_t *s)
{
    flux_watcher_destroy (s->terminate_timer_w);
    flux_watcher_destroy (s->terminate_prep_w);
    flux_watcher_destroy (s->terminate_idle_w);
    flux_watcher_destroy (s->terminate_check_w);
    s->terminate_timer_w = NULL;
    s->terminate_prep_w = NULL;
    s->terminate_idle_w = NULL;
    s->terminate_check_w = NULL;
}

int server_terminate_setup (flux_subprocess_server_t *s, double wait_time)
{
    s->terminate_timer_w =
        flux_timer_watcher_create (s->r, wait_time, 0., terminate_cb, s);
    if (!s->terminate_timer_w) {
        flux_log_error (s->h, "flux_timer_watcher_create");
        goto error;
    }

    if (s->terminate_prep_w)
        return 0;

    s->terminate_prep_w = flux_prepare_watcher_create (s->r, terminate_prep_cb, s);
    if (!s->terminate_prep_w) {
        flux_log_error (s->h, "flux_prepare_watcher_create");
        goto error;
    }

    s->terminate_idle_w = flux_idle_watcher_create (s->r, NULL, s);
    if (!s->terminate_idle_w) {
        flux_log_error (s->h, "flux_idle_watcher_create");
        goto error;
    }

    s->terminate_check_w = flux_check_watcher_create (s->r, terminate_cb, s);
    if (!s->terminate_check_w) {
        flux_log_error (s->h, "flux_check_watcher_create");
        goto error;
    }

    return 0;

error:
    server_terminate_cleanup (s);
    return -1;
}

int server_terminate_wait (flux_subprocess_server_t *s)
{
    flux_watcher_start (s->terminate_timer_w);

    if (flux_reactor_run (s->r, 0) < 0) {
        flux_log_error (s->h, "flux_reactor_run");
        return -1;
    }

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
