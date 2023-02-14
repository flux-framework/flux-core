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

#include <unistd.h> // defines environ
#include <errno.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "server.h"

/* Keys used to store subprocess server, rexec.exec request, and
 * 'subprocesses' zlistx handle in the subprocess object.
 */
static const char *srvkey = "flux::server";
static const char *msgkey = "flux::request";
static const char *lstkey = "flux::handle";

struct subprocess_server {
    flux_t *h;
    flux_reactor_t *r;
    char *local_uri;
    uint32_t rank;
    zlistx_t *subprocesses;
    flux_msg_handler_t **handlers;
    subprocess_server_auth_f auth_cb;
    void *arg;

    /* for teardown / termination */
    flux_watcher_t *terminate_timer_w;
    flux_watcher_t *terminate_prep_w;
    flux_watcher_t *terminate_idle_w;
    flux_watcher_t *terminate_check_w;
};

// zlistx_destructor_fn footprint
static void proc_destructor (void **item)
{
    if (item) {
        flux_subprocess_unref (*item);
        *item = NULL;
    }
}

static int proc_save (subprocess_server_t *s, flux_subprocess_t *p)
{
    void *handle;

    if (!(handle = zlistx_add_end (s->subprocesses, p))) {
        errno = ENOMEM;
        return -1;
    }
    if (flux_subprocess_aux_set (p, lstkey, handle, NULL) < 0) {
        int saved_errno = errno;
        zlistx_detach (s->subprocesses, handle);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static void proc_delete (subprocess_server_t *s, flux_subprocess_t *p)
{
    int saved_errno = errno;
    void *handle = flux_subprocess_aux_get (p, lstkey);

    zlistx_delete (s->subprocesses, handle);

    if (!zlistx_size (s->subprocesses) && s->terminate_prep_w) {
        flux_watcher_start (s->terminate_prep_w);
        flux_watcher_start (s->terminate_check_w);
    }

    errno = saved_errno;
}

static flux_subprocess_t *proc_find_bypid (subprocess_server_t *s, pid_t pid)
{
    flux_subprocess_t *p;

    p = zlistx_first (s->subprocesses);
    while (p) {
        if (flux_subprocess_pid (p) == pid)
            return p;
        p = zlistx_next (s->subprocesses);
    }
    errno = ENOENT;
    return NULL;
}

static void proc_completion_cb (flux_subprocess_t *p)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);

    if (p->state != FLUX_SUBPROCESS_FAILED) {
        /* no fallback if this fails */
        if (flux_respond_error (s->h, request, ENODATA, NULL) < 0)
            flux_log_error (s->h, "error responding to rexec.exec request");
    }

    proc_delete (s, p);
}

static void proc_internal_fatal (flux_subprocess_t *p)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);

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

static void proc_state_change_cb (flux_subprocess_t *p,
                                  flux_subprocess_state_t state)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);
    int rc = 0;

    if (state == FLUX_SUBPROCESS_RUNNING) {
        rc = flux_respond_pack (s->h, request, "{s:s s:i s:i s:i}",
                                "type", "state",
                                "rank", s->rank,
                                "pid", flux_subprocess_pid (p),
                                "state", state);
    }
    else if (state == FLUX_SUBPROCESS_EXITED) {
        rc = flux_respond_pack (s->h, request, "{s:s s:i s:i s:i}",
                                "type", "state",
                                "rank", s->rank,
                                "state", state,
                                "status", flux_subprocess_status (p));
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        rc = flux_respond_error (s->h, request, p->failed_errno, NULL);
        proc_delete (s, p); // N.B. proc_delete preserves errno
    } else {
        errno = EPROTO;
        flux_log_error (s->h, "%s: illegal state", __FUNCTION__);
        goto error;
    }
    if (rc < 0)
        flux_log_error (s->h, "error responding to rexec.exec request");

    return;

error:
    proc_internal_fatal (p);
}

static int proc_output (flux_subprocess_t *p,
                        const char *stream,
                        subprocess_server_t *s,
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
        flux_log_error (s->h, "error responding to rexec.exec request");
        goto error;
    }

    rv = 0;
error:
    json_decref (io);
    return rv;
}

static void proc_output_cb (flux_subprocess_t *p, const char *stream)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);
    const char *ptr;
    int lenp;

    if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
        flux_log_error (s->h, "%s: flux_subprocess_read", __FUNCTION__);
        goto error;
    }

    if (lenp) {
        if (proc_output (p, stream, s, request, ptr, lenp, false) < 0)
            goto error;
    }
    else {
        if (proc_output (p, stream, s, request, NULL, 0, true) < 0)
            goto error;
    }

    return;

error:
    proc_internal_fatal (p);
}

static void server_exec_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    subprocess_server_t *s = arg;
    const char *cmd_str;
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = proc_completion_cb,
        .on_state_change = proc_state_change_cb,
        .on_channel_out = proc_output_cb,
        .on_stdout = proc_output_cb,
        .on_stderr = proc_output_cb,
    };
    int on_channel_out, on_stdout, on_stderr;
    char **env = NULL;
    const char *errmsg = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg, NULL, "{s:s s:i s:i s:i}",
                             "cmd", &cmd_str,
                             "on_channel_out", &on_channel_out,
                             "on_stdout", &on_stdout,
                             "on_stderr", &on_stderr))
        goto error;
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!on_channel_out)
        ops.on_channel_out = NULL;
    if (!on_stdout)
        ops.on_stdout = NULL;
    if (!on_stderr)
        ops.on_stderr = NULL;

    if (!(cmd = flux_cmd_fromjson (cmd_str, NULL))) {
        errmsg = "error parsing command string";
        goto error;
    }

    if (!flux_cmd_argc (cmd)) {
        errno = EPROTO;
        errmsg = "command string is empty";
        goto error;
    }

    /* if no environment sent, use local server environment */
    if (!(env = flux_cmd_env_expand (cmd))
        || (env[0] == NULL && flux_cmd_set_env (cmd, environ))
        || flux_cmd_setenvf (cmd, 1, "FLUX_URI", "%s", s->local_uri) < 0) {
        errmsg = "error setting up command environment";
        goto error;
    }

    if (!(p = flux_exec (s->h,
                         FLUX_SUBPROCESS_FLAGS_SETPGRP,
                         cmd,
                         &ops,
                         NULL))) {
        errmsg = "exec failed";
        goto error;
    }

    if (flux_subprocess_aux_set (p,
                                msgkey,
                                (void *)flux_msg_incref (msg),
                                (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error;
    }
    if (flux_subprocess_aux_set (p, srvkey, s, NULL) < 0)
        goto error;
    if (proc_save (s, p) < 0)
        goto error;

    flux_cmd_destroy (cmd);
    free (env);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (s->h, "error responding to rexec.exec request");
    flux_cmd_destroy (cmd);
    free (env);
    flux_subprocess_unref (p);
}

static int write_subprocess (subprocess_server_t *s, flux_subprocess_t *p,
                             const char *stream, const char *data, int len)
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

static int close_subprocess (subprocess_server_t *s, flux_subprocess_t *p,
                             const char *stream)
{
    if (flux_subprocess_close (p, stream) < 0) {
        flux_log_error (s->h, "%s: flux_subprocess_close", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void server_write_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    flux_subprocess_t *p;
    subprocess_server_t *s = arg;
    const char *stream = NULL;
    char *data = NULL;
    int len = 0;
    bool eof = false;
    pid_t pid;
    json_t *io = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg, NULL, "{ s:i s:o }",
                             "pid", &pid,
                             "io", &io) < 0) {
        /* can't handle error, no pid to sent errno back to, so just
         * return */
        flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errno = EPERM;
        flux_log_error (s->h, "rexec.write: %s", error.text);
        return;
    }

    if (iodecode (io, &stream, NULL, &data, &len, &eof) < 0) {
        flux_log_error (s->h, "%s: iodecode", __FUNCTION__);
        return;
    }

    if (!(p = proc_find_bypid (s, pid))) {
        /* can't handle error, no pid to send errno back to, so just
         * return
         *
         * It's common on EOF to be sent and server has already
         * removed process from hash.  Don't output error in that
         * case.
         */
        if (!(errno == ENOENT && eof))
            flux_log_error (s->h, "%s: proc_find_bypid", __FUNCTION__);
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
    proc_internal_fatal (p);
}

static void server_signal_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    subprocess_server_t *s = arg;
    pid_t pid;
    int signum;
    flux_error_t error;
    const char *errmsg = NULL;

    errno = 0;

    if (flux_request_unpack (msg, NULL, "{ s:i s:i }",
                             "pid", &pid,
                             "signum", &signum) < 0) {
        flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
        errno = EPROTO;
        goto error;
    }
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!proc_find_bypid (s, pid))
        goto error;

    if (killpg (pid, signum) < 0) {
        flux_log_error (s->h, "kill");
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static const char *subprocess_sender (flux_subprocess_t *p)
{
    const flux_msg_t *msg = flux_subprocess_aux_get (p, msgkey);
    return flux_msg_route_first (msg);
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

static void server_processes_cb (flux_t *h, flux_msg_handler_t *mh,
                                 const flux_msg_t *msg, void *arg)
{
    subprocess_server_t *s = arg;
    flux_subprocess_t *p;
    json_t *procs = NULL;
    flux_error_t error;
    const char *errmsg = NULL;

    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!(procs = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    p = zlistx_first (s->subprocesses);
    while (p) {
        json_t *o = NULL;
        if (!(o = process_info (p))
            || json_array_append_new (procs, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }
        p = zlistx_next (s->subprocesses);
    }

    if (flux_respond_pack (h, msg, "{s:i s:o}", "rank", s->rank,
                           "procs", procs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (procs);
}

static void server_disconnect_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    subprocess_server_t *s = arg;
    const char *sender;

    if ((sender = flux_msg_route_first (msg)))
        subprocess_server_terminate_by_uuid (s, sender);
}

static int server_start (subprocess_server_t *s)
{
    /* rexec.processes is primarily for testing */
    struct flux_msg_handler_spec htab[] = {
        { FLUX_MSGTYPE_REQUEST,
          "rexec.exec",
          server_exec_cb,
          0
        },
        { FLUX_MSGTYPE_REQUEST,
          "rexec.write",
          server_write_cb,
          0
        },
        { FLUX_MSGTYPE_REQUEST,
          "rexec.signal",
          server_signal_cb,
          0
        },
        { FLUX_MSGTYPE_REQUEST,
          "rexec.processes",
          server_processes_cb,
          0
        },
        { FLUX_MSGTYPE_REQUEST,
          "rexec.disconnect",
          server_disconnect_cb,
          0
        },
        FLUX_MSGHANDLER_TABLE_END,
    };

    if (flux_msg_handler_addvec (s->h, htab, s, &s->handlers) < 0)
        return -1;

    return 0;
}

static void server_stop (subprocess_server_t *s)
{
    flux_msg_handler_delvec (s->handlers);
}

static void server_signal_subprocess (flux_subprocess_t *p, int signum)
{
    flux_future_t *f;
    if (!(f = flux_subprocess_kill (p, signum))) {
        subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);

        flux_log_error (s->h, "%s: flux_subprocess_kill", __FUNCTION__);
        return;
    }
    flux_future_destroy (f);
}

static int server_signal_subprocesses (subprocess_server_t *s, int signum)
{
    flux_subprocess_t *p;

    p = zlistx_first (s->subprocesses);
    while (p) {
        server_signal_subprocess (p, signum);
        p = zlistx_next (s->subprocesses);
    }

    return 0;
}

int subprocess_server_terminate_by_uuid (subprocess_server_t *s,
                                         const char *id)
{
    flux_subprocess_t *p;

    if (!s || !id) {
        errno = EINVAL;
        return -1;
    }
    p = zlistx_first (s->subprocesses);
    while (p) {
        const char *sender = subprocess_sender (p);
        if (sender && streq (id, sender))
            server_signal_subprocess (p, SIGKILL);
        p = zlistx_next (s->subprocesses);
    }
    return 0;
}

static void terminate_prep_cb (flux_reactor_t *r,
                               flux_watcher_t *w,
                               int revents,
                               void *arg)
{
    subprocess_server_t *s = arg;
    flux_watcher_start (s->terminate_idle_w);
}

static void terminate_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    subprocess_server_t *s = arg;
    flux_watcher_stop (s->terminate_timer_w);
    flux_watcher_stop (s->terminate_prep_w);
    flux_watcher_stop (s->terminate_idle_w);
    flux_watcher_stop (s->terminate_check_w);
    flux_reactor_stop (s->r);
}

static void server_terminate_cleanup (subprocess_server_t *s)
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

static int server_terminate_setup (subprocess_server_t *s,
                            double wait_time)
{
    s->terminate_timer_w = flux_timer_watcher_create (s->r,
                                                      wait_time, 0.,
                                                      terminate_cb,
                                                      s);
    if (!s->terminate_timer_w) {
        flux_log_error (s->h, "flux_timer_watcher_create");
        goto error;
    }

    if (s->terminate_prep_w)
        return 0;

    s->terminate_prep_w = flux_prepare_watcher_create (s->r,
                                                       terminate_prep_cb,
                                                       s);
    if (!s->terminate_prep_w) {
        flux_log_error (s->h, "flux_prepare_watcher_create");
        goto error;
    }

    s->terminate_idle_w = flux_idle_watcher_create (s->r,
                                                    NULL,
                                                    s);
    if (!s->terminate_idle_w) {
        flux_log_error (s->h, "flux_idle_watcher_create");
        goto error;
    }

    s->terminate_check_w = flux_check_watcher_create (s->r,
                                                      terminate_cb,
                                                      s);
    if (!s->terminate_check_w) {
        flux_log_error (s->h, "flux_check_watcher_create");
        goto error;
    }

    return 0;

error:
    server_terminate_cleanup (s);
    return -1;
}

static int server_terminate_wait (subprocess_server_t *s)
{
    flux_watcher_start (s->terminate_timer_w);

    if (flux_reactor_run (s->r, 0) < 0) {
        flux_log_error (s->h, "flux_reactor_run");
        return -1;
    }

    return 0;
}

static void subprocess_server_destroy (void *arg)
{
    subprocess_server_t *s = arg;
    if (s) {
        int saved_errno = errno;
        /* s->handlers handled in server_stop, this is for destroying
         * things only
         */
        zlistx_destroy (&s->subprocesses);
        free (s->local_uri);

        flux_watcher_destroy (s->terminate_timer_w);
        flux_watcher_destroy (s->terminate_prep_w);
        flux_watcher_destroy (s->terminate_idle_w);
        flux_watcher_destroy (s->terminate_check_w);

        free (s);
        errno = saved_errno;
    }
}

static subprocess_server_t *subprocess_server_create (flux_t *h,
                                                      const char *local_uri,
                                                      int rank)
{
    subprocess_server_t *s = calloc (1, sizeof (*s));

    if (!s)
        return NULL;

    s->h = h;
    if (!(s->r = flux_get_reactor (h)))
        goto error;
    if (!(s->subprocesses = zlistx_new ()))
        goto error;
    zlistx_set_destructor (s->subprocesses, proc_destructor);
    if (!(s->local_uri = strdup (local_uri)))
        goto error;
    s->rank = rank;

    return s;

error:
    subprocess_server_destroy (s);
    return NULL;
}


subprocess_server_t *subprocess_server_start (flux_t *h,
                                              const char *local_uri,
                                              uint32_t rank)
{
    subprocess_server_t *s = NULL;

    if (!h || !local_uri) {
        errno = EINVAL;
        goto error;
    }

    if (!(s = subprocess_server_create (h, local_uri, rank)))
        goto error;

    if (server_start (s) < 0)
        goto error;

    return s;

error:
    subprocess_server_destroy (s);
    return NULL;
}

void subprocess_server_set_auth_cb (subprocess_server_t *s,
                                    subprocess_server_auth_f fn,
                                    void *arg)
{
    s->auth_cb = fn;
    s->arg = arg;
}

void subprocess_server_stop (subprocess_server_t *s)
{
    if (s) {
        server_stop (s);
        server_signal_subprocesses (s, SIGKILL);
        subprocess_server_destroy (s);
    }
}

int subprocess_server_subprocesses_kill (subprocess_server_t *s,
                                         int signum,
                                         double wait_time)
{
    int rv = -1;

    if (!s) {
        errno = EINVAL;
        return -1;
    }

    if (!zlistx_size (s->subprocesses))
        return 0;

    if (server_terminate_setup (s, wait_time) < 0)
        goto error;

    if (server_signal_subprocesses (s, signum) < 0)
        goto error;

    if (server_terminate_wait (s) < 0)
        goto error;

    rv = 0;
error:
    server_terminate_cleanup (s);
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
