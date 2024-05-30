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
#include <signal.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/llog.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command_private.h"
#include "server.h"
#include "client.h"

/* Keys used to store subprocess server, rexec.exec request, and
 * 'subprocesses' zlistx handle in the subprocess object.
 */
static const char *srvkey = "flux::server";
static const char *msgkey = "flux::request";
static const char *lstkey = "flux::handle";

struct subprocess_server {
    flux_t *h;
    char *local_uri;
    uint32_t rank;
    subprocess_log_f llog;
    void *llog_data;
    zlistx_t *subprocesses;
    flux_msg_handler_t **handlers;
    subprocess_server_auth_f auth_cb;
    void *arg;
    // The shutdown future is created when user calls shutdown,
    //  and fulfilled once subprocesses list becomes empty.
    flux_future_t *shutdown;
};

static void server_kill (flux_subprocess_t *p, int signum);

// zlistx_destructor_fn footprint
static void proc_destructor (void **item)
{
    if (item) {
        subprocess_decref (*item);
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

    if (zlistx_size (s->subprocesses) == 0 && s->shutdown)
        flux_future_fulfill (s->shutdown, NULL, NULL);

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
    errno = ESRCH;
    return NULL;
}

static void proc_completion_cb (flux_subprocess_t *p)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);

    if (p->state != FLUX_SUBPROCESS_FAILED) {
        /* no fallback if this fails */
        if (flux_respond_error (s->h, request, ENODATA, NULL) < 0) {
            llog_error (s,
                        "error responding to rexec.exec request: %s",
                        strerror (errno));
        }
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
    errprintf (&p->failed_error, "internal fatal error: %s", strerror (errno));
    state_change_start (p);

    /* if we fail here, probably not much can be done */
    if (killpg (p->pid, SIGKILL) < 0) {
        if (errno != ESRCH) {
            llog_error (s,
                        "killpg %d SIGKILL: %s",
                        (int)p->pid,
                        strerror (errno));
        }
    }
}

static void proc_state_change_cb (flux_subprocess_t *p,
                                  flux_subprocess_state_t state)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);
    int rc = 0;

    if (state == FLUX_SUBPROCESS_RUNNING) {
        rc = flux_respond_pack (s->h,
                                request,
                                "{s:s s:i}",
                                "type", "started",
                                "pid", flux_subprocess_pid (p));
    }
    else if (state == FLUX_SUBPROCESS_EXITED) {
        rc = flux_respond_pack (s->h,
                                request,
                                "{s:s s:i}",
                                "type", "finished",
                                "status", flux_subprocess_status (p));
    }
    else if (state == FLUX_SUBPROCESS_STOPPED) {
        rc = flux_respond_pack (s->h,
                                request,
                                "{s:s}",
                                "type", "stopped");
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        const char *errmsg = NULL;
        if (p->failed_error.text[0] != '\0')
            errmsg = p->failed_error.text;
        rc = flux_respond_error (s->h,
                                 request,
                                 p->failed_errno,
                                 errmsg);
        proc_delete (s, p); // N.B. proc_delete preserves errno
    } else {
        errno = EPROTO;
        llog_error (s, "subprocess entered illegal state %d", state);
        goto error;
    }
    if (rc < 0) {
        llog_error (s,
                    "error responding to rexec.exec request: %s",
                    strerror (errno));
    }
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
        llog_error (s, "ioencode %s: %s", stream, strerror (errno));
        goto error;
    }

    if (flux_respond_pack (s->h,
                           msg,
                           "{s:s s:i s:O}",
                           "type", "output",
                           "pid", flux_subprocess_pid (p),
                           "io", io) < 0) {
        llog_error (s,
                    "error responding to rexec.exec request: %s",
                    strerror (errno));
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
    const char *buf;
    int len;

    len = flux_subprocess_getline (p, stream, &buf);
    if (len < 0 && errno == EPERM) // not line buffered
        len = flux_subprocess_read (p, stream, &buf);
    if (len < 0) {
        llog_error (s,
                    "error reading from subprocess stream %s: %s",
                    stream,
                    strerror (errno));
        goto error;
    }

    if (len) {
        if (proc_output (p, stream, s, request, buf, len, false) < 0)
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

static void server_exec_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    subprocess_server_t *s = arg;
    json_t *cmd_obj;
    flux_cmd_t *cmd = NULL;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = proc_completion_cb,
        .on_state_change = proc_state_change_cb,
        .on_channel_out = proc_output_cb,
        .on_stdout = proc_output_cb,
        .on_stderr = proc_output_cb,
    };
    char **env = NULL;
    const char *errmsg = NULL;
    flux_error_t error;
    int flags;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:i}",
                             "cmd", &cmd_obj,
                             "flags", &flags) < 0)
        goto error;
    if (s->shutdown) {
        errmsg = "subprocess server is shutting down";
        errno = ENOSYS;
        goto error;
    }
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!(flags & SUBPROCESS_REXEC_CHANNEL))
        ops.on_channel_out = NULL;
    if (!(flags & SUBPROCESS_REXEC_STDOUT))
        ops.on_stdout = NULL;
    if (!(flags & SUBPROCESS_REXEC_STDERR))
        ops.on_stderr = NULL;

    if (!(cmd = cmd_fromjson (cmd_obj, NULL))) {
        errmsg = "error parsing command string";
        goto error;
    }

    if (!flux_cmd_argc (cmd)) {
        errno = EPROTO;
        errmsg = "command string is empty";
        goto error;
    }

    /* if no environment sent, use local server environment */
    if (!(env = cmd_env_expand (cmd))
        || (env[0] == NULL && cmd_set_env (cmd, environ))
        || flux_cmd_setenvf (cmd, 1, "FLUX_URI", "%s", s->local_uri) < 0) {
        errmsg = "error setting up command environment";
        goto error;
    }

    /* Never propagate FLUX_PROXY_REMOTE to processes started from
     * a subprocess server.
     */
    flux_cmd_unsetenv (cmd, "FLUX_PROXY_REMOTE");

    if (!(p = flux_local_exec_ex (flux_get_reactor (s->h),
                                  FLUX_SUBPROCESS_FLAGS_SETPGRP,
                                  cmd,
                                  &ops,
                                  NULL,
                                  s->llog,
                                  s->llog_data))) {
        errprintf (&error, "error launching process: %s", strerror (errno));
        errmsg = error.text;
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
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        llog_error (s,
                    "error responding to rexec.exec request: %s",
                    strerror (errno));
    }
    flux_cmd_destroy (cmd);
    free (env);
    subprocess_decref (p);
}

static void server_write_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
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

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s:o }",
                             "pid", &pid,
                             "io", &io) < 0
        || iodecode (io, &stream, NULL, &data, &len, &eof) < 0) {
        llog_error (s,
                    "Error decoding rexec.write request: %s",
                    strerror (errno));
        goto out;
    }
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        llog_error (s, "rexec.write: %s", error.text);
        goto out;
    }

    /* If the subprocess can't be found or is no longer running, just silently
     * drop the data. This is expected if tasks are killed or exit with data
     * in flight, and is not necessarily an error, and can be common enough
     * that the log messages end up being a nuisance.
     */
    if (!(p = proc_find_bypid (s, pid))
        || p->state != FLUX_SUBPROCESS_RUNNING)
        goto out;

    if (data && len) {
        int rc = flux_subprocess_write (p, stream, data, len);
        if (rc < 0) {
            llog_error (s,
                        "Error writing %d bytes to subprocess pid %d %s",
                        len,
                        (int)pid,
                        stream);
            goto error;
        }
    }
    if (eof) {
        if (flux_subprocess_close (p, stream) < 0) {
            llog_error (s,
                        "Error writing EOF to subprocess pid %d %s",
                        (int)pid,
                        stream);
            goto error;
        }
    }

out:
    free (data);
    return;

error:
    free (data);
    proc_internal_fatal (p);
}

static void server_kill_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    subprocess_server_t *s = arg;
    pid_t pid;
    int signum;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s:i }",
                             "pid", &pid,
                             "signum", &signum) < 0)
        goto error;
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!proc_find_bypid (s, pid)
        || killpg (pid, signum) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0) {
        llog_error (s,
                    "error responding to rexec.kill request: %s",
                    strerror (errno));
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        llog_error (s,
                    "error responding to rexec.kill request: %s",
                    strerror (errno));
    }
}

static const char *subprocess_sender (flux_subprocess_t *p)
{
    const flux_msg_t *msg = flux_subprocess_aux_get (p, msgkey);
    return flux_msg_route_first (msg);
}

static json_t *process_info (flux_subprocess_t *p)
{
    flux_cmd_t *cmd;
    json_t *info = NULL;
    char *s;

    if (!(cmd = flux_subprocess_get_cmd (p))
            || !(s = flux_cmd_stringify (cmd)))
        return NULL;
    if (!(info = json_pack ("{s:i s:s}",
                            "pid", flux_subprocess_pid (p),
                            "cmd", flux_cmd_arg (cmd, 0)))) {
        free (s);
        errno = ENOMEM;
        return NULL;
    }
    free (s);
    return info;
}

static void server_list_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
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
    if (!(procs = json_array ()))
        goto nomem;
    p = zlistx_first (s->subprocesses);
    while (p) {
        json_t *o = NULL;
        if (!(o = process_info (p))
            || json_array_append_new (procs, o) < 0) {
            json_decref (o);
            goto nomem;
        }
        p = zlistx_next (s->subprocesses);
    }
    if (flux_respond_pack (h, msg, "{s:i s:o}", "rank", s->rank,
                           "procs", procs) < 0) {
        llog_error (s,
                    "error responding to rexec.list request: %s",
                    strerror (errno));
    }
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        llog_error (s,
                    "error responding to rexec.list request: %s",
                    strerror (errno));
    }
    json_decref (procs);
}

static void server_disconnect_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    subprocess_server_t *s = arg;
    const char *sender;

    if ((sender = flux_msg_route_first (msg))) {
        flux_subprocess_t *p;
        p = zlistx_first (s->subprocesses);
        while (p) {
            const char *uuid = subprocess_sender (p);
            if (sender && streq (uuid, sender))
                server_kill (p, SIGKILL);
            p = zlistx_next (s->subprocesses);
        }
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "exec",
      server_exec_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "write",
      server_write_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "kill",
      server_kill_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "list",
      server_list_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "disconnect",
      server_disconnect_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void server_kill (flux_subprocess_t *p, int signum)
{
    flux_future_t *f;
    if (!(f = flux_subprocess_kill (p, signum))) {
        subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
        llog_error (s,
                    "subprocess_kill %d %d: %s",
                    p->pid,
                    signum,
                    strerror (errno));
        return;
    }
    flux_future_destroy (f);
}

static int server_killall (subprocess_server_t *s, int signum)
{
    flux_subprocess_t *p;

    p = zlistx_first (s->subprocesses);
    while (p) {
        server_kill (p, signum);
        p = zlistx_next (s->subprocesses);
    }

    return 0;
}

void subprocess_server_destroy (subprocess_server_t *s)
{
    if (s) {
        int saved_errno = errno;
        flux_msg_handler_delvec (s->handlers);
        server_killall (s, SIGKILL);
        zlistx_destroy (&s->subprocesses);
        flux_future_destroy (s->shutdown);
        free (s->local_uri);
        free (s);
        errno = saved_errno;
    }
}

subprocess_server_t *subprocess_server_create (flux_t *h,
                                               const char *service_name,
                                               const char *local_uri,
                                               subprocess_log_f log_fn,
                                               void *log_data)
{
    subprocess_server_t *s;

    if (!h || !local_uri || !service_name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(s = calloc (1, sizeof (*s))))
        return NULL;

    s->h = h;

    s->llog = log_fn;
    s->llog_data = log_data;

    if (!(s->subprocesses = zlistx_new ()))
        goto error;
    zlistx_set_destructor (s->subprocesses, proc_destructor);
    if (!(s->local_uri = strdup (local_uri)))
        goto error;
    if (flux_get_rank (h, &s->rank) < 0)
        goto error;
    if (flux_msg_handler_addvec_ex (s->h,
                                    service_name,
                                    htab,
                                    s,
                                    &s->handlers) < 0)
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

flux_future_t *subprocess_server_shutdown (subprocess_server_t *s, int signum)
{
    flux_future_t *f;

    if (!s || s->shutdown != NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    flux_future_set_reactor (f, flux_get_reactor (s->h));
    flux_future_set_flux (f, s->h);
    flux_future_incref (f);
    s->shutdown = f;
    if (zlistx_size (s->subprocesses) == 0)
        flux_future_fulfill (f, NULL, NULL);
    else
        server_killall (s, signum);
    return f;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
