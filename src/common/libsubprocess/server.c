/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* libsubprocess server - remote subprocess execution service
 *
 * OVERVIEW
 * --------
 * This module implements a subprocess server that executes processes
 * on behalf of remote clients via the subprocess protocol defined in
 * RFC 42. The server handles subprocess lifecycle management including
 * execution, I/O streaming, signal delivery, and process cleanup.
 *
 * RPC ENDPOINTS
 * -------------
 * exec       - Start a new subprocess (streaming or background)
 * write      - Write data to subprocess stdin
 * kill       - Send signal to subprocess
 * list       - List active and zombie subprocesses
 * wait       - Wait for and collect exit status of waitable subprocess
 * disconnect - Notify server of client disconnect
 *
 * EXEC REQUEST TYPES
 * -------------------
 * 1. Streaming (foreground): Default mode. Server streams stdout/stderr back
 *    to client via streaming RPC responses and delivers final exit status
 *    in the "finished" response. Process is removed from server list
 *    after final response is sent.
 *
 * 2. Non-streaming (background): Process runs detached. Server does not
 *    stream output or send status responses. Process is removed from server
 *    list immediately upon exit unless marked waitable.
 *
 * 3. Waitable: Background process enters zombie state upon exit, remaining
 *    in server list until status is collected via wait RPC or server is
 *    shutdown. See RFC 42 for protocol details.
 *
 * PROCESS LIFECYCLE
 * -----------------
 * Normal subprocess:
 *   exec -> running -> exited -> [removed from list]
 *
 * Waitable subprocess:
 *   exec -> running -> exited -> zombie -> wait RPC -> [removed from list]
 *
 * WAITABLE SUBPROCESS BEHAVIOR
 * ----------------------------
 * - Only background processes can be waitable.
 *
 * - Processes that fail to start are never kept as zombies, regardless of
 *   waitable flag. They are immediately removed with error response sent
 *   to client.
 *
 * - Zombie processes remain in the list with state "Z" until:
 *   a) A client successfully calls the wait RPC
 *   b) Server shutdown (zombies are purged during shutdown sequence)
 *
 * - Only one concurrent waiter per process is allowed. Second wait attempt
 *   returns EINVAL.
 *
 * - If a waiting client disconnects before process exits, the pending wait
 *   is cancelled and another client may wait for the process.
 *
 * - wait RPC can be called before or after process exit:
 *   - Before exit: RPC blocks until process exits, then returns status
 *   - After exit: RPC returns immediately with cached status
 *
 * - Once status is collected process is removed from server list and cannot
 *   be waited on again.
 *
 * DISCONNECT HANDLING
 * -------------------
 * When a client disconnects:
 * - Streaming (non-background) processes from that client are killed with
 *   SIGKILL
 * - Any pending wait RPC from that client is cancelled
 * - Background processes continue running regardless of client disconnect
 *
 * IMPLEMENTATION NOTES
 * --------------------
 * - Process list maintained in s->subprocesses (zlistx)
 * - Process lookup by PID via iteration, by label via s->labels hash
 * - p->waiter stores pending wait RPC message if wait is in progress
 * - proc_delete() handles wait notification and process deletion
 */

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
#include "src/common/libutil/basename.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command_private.h"
#include "server.h"
#include "client.h"
#include "util.h"
#include "sigchld.h"

extern char **environ;

/* Keys used to store subprocess server, exec request
 * (i.e. rexec.exec), and 'subprocesses' zlistx handle in the
 * subprocess object.
 */
static const char *srvkey = "flux::server";
static const char *msgkey = "flux::request";
static const char *lstkey = "flux::handle";

struct subprocess_server {
    flux_t *h;
    char *service_name;
    char *local_uri;
    uint32_t rank;
    subprocess_log_f llog;
    void *llog_data;
    zlistx_t *subprocesses;
    zhashx_t *labels;
    flux_msg_handler_t **handlers;
    subprocess_server_auth_f auth_cb;
    void *arg;
    // The shutdown future is created when user calls shutdown,
    //  and fulfilled once subprocesses list becomes empty.
    flux_future_t *shutdown;
    bool has_sigchld_ctx;
};

static void server_kill (flux_subprocess_t *p, int signum);

static inline bool is_waitable (flux_subprocess_t *p)
{
    /* Currently, processes are only waitable if they have the waitable
     * flag and they are in background mode.
     */
    return (p->bg && p->waitable);
}

static inline void clear_waitable (flux_subprocess_t *p)
{
    p->waitable = false;
    flux_msg_decref (p->waiter);
    p->waiter = NULL;
}

/* If subprocess is waitable, complete, and has a waiter, respond
 * with status and clear waitable flag and waiter. Otherwise, do nothing.
 */
static void wait_notify (subprocess_server_t *s, flux_subprocess_t *p)
{
    if (is_waitable (p) && !flux_subprocess_active (p) && p->waiter) {
        if (flux_respond_pack (s->h,
                               p->waiter,
                               "{s:i}",
                               "status", flux_subprocess_status (p)) < 0)
            llog_error (s, "wait respond pid %d", flux_subprocess_pid (p));
        clear_waitable (p);
    }
}

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
    const char *label = flux_cmd_get_label (p->cmd);

    if (!(handle = zlistx_add_end (s->subprocesses, p))) {
        if (label)
            zhashx_delete (s->labels, label);
        errno = ENOMEM;
        return -1;
    }
    if (flux_subprocess_aux_set (p, lstkey, handle, NULL) < 0
        || (label && zhashx_insert (s->labels, label, p) < 0)) {
        int saved_errno = errno;
        zlistx_detach (s->subprocesses, handle);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static void proc_delete (subprocess_server_t *s, flux_subprocess_t *p)
{
    const char *label;
    int saved_errno = errno;
    void *handle = flux_subprocess_aux_get (p, lstkey);

    /* Notify waiter if there is one and this process is complete.
     * Clears waitable flag if notification occurs.
     */
    wait_notify (s, p);

    /* Processes that are still waitable (i.e. have the waitable flag but
     * no current waiter) stay in subprocesses list until an wait RPC or
     * the server is shutdown.
     */
    if (is_waitable (p))
        goto out;

    /* Otherwise, process can be deleted:
     */
    if ((label = flux_cmd_get_label (p->cmd)))
        zhashx_delete (s->labels, label);

    zlistx_delete (s->subprocesses, handle);

    if (zlistx_size (s->subprocesses) == 0 && s->shutdown)
        flux_future_fulfill (s->shutdown, NULL, NULL);
out:
    errno = saved_errno;
}

static flux_subprocess_t *proc_find_bylabel (subprocess_server_t *s,
                                             const char *label)
{
    flux_subprocess_t *p;
    if ((p = zhashx_lookup (s->labels, label)))
        return p;
    errno = ESRCH;
    return NULL;
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

/* Find a <service>.exec message with the same sender as msg and matchtag as
 * specified in the request matchtag field.
 * N.B. flux_cancel_match() happens to be helpful because RFC 42 subprocess
 * write works like RFC 6 cancel.
 */
static flux_subprocess_t *proc_find_byclient (subprocess_server_t *s,
                                              const flux_msg_t *request)
{
    flux_subprocess_t *p;

    p = zlistx_first (s->subprocesses);
    while (p) {
        const flux_msg_t *msg;

        if ((msg = flux_subprocess_aux_get (p, msgkey))
            && flux_cancel_match (request, msg))
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

    if (p->bg) {
        int exitcode;
        flux_cmd_t *cmd = flux_subprocess_get_cmd (p);
        const char *label = flux_cmd_get_label (cmd);

        if ((exitcode = flux_subprocess_exit_code (p)) < 0) {
            llog_info (s,
                       "%s%s%s[%d]: Killed by signal %d",
                       label ? label : "",
                       label ? ": " : "",
                       flux_cmd_arg (cmd, 0),
                       (int) p->pid,
                       flux_subprocess_signaled (p));
        }
        else {
            const char *command = basename_simple (flux_cmd_arg (cmd, 0));
            llog_info (s,
                       "%s%s%s[%d]: Exit %d",
                       label ? label : "",
                       label ? ": " : "",
                       command,
                       (int) p->pid,
                       exitcode);
        }
    }
    else if (p->state != FLUX_SUBPROCESS_FAILED) {
        /* no fallback if this fails */
        if (flux_respond_error (s->h, request, ENODATA, NULL) < 0) {
            llog_error (s,
                        "error responding to %s.exec request: %s",
                        s->service_name,
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
        /* If this is a background process, remove request from subprocess
         * aux item list, since it will no longer be valid after this point.
         */
        if (p->bg)
            flux_subprocess_aux_set (p, msgkey, NULL, NULL);
    }
    else if (state == FLUX_SUBPROCESS_EXITED) {
        if (!p->bg)
            rc = flux_respond_pack (s->h,
                                    request,
                                    "{s:s s:i}",
                                    "type", "finished",
                                    "status", flux_subprocess_status (p));
    }
    else if (state == FLUX_SUBPROCESS_STOPPED) {
        if (!p->bg)
            rc = flux_respond_pack (s->h,
                                    request,
                                    "{s:s}",
                                    "type", "stopped");
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        const char *errmsg = NULL;
        if (p->failed_error.text[0] != '\0')
            errmsg = p->failed_error.text;

        /* N.B. background process may also fail here, but check for
         * valid request before responding in case request has been
         * cleared.
         */
        if (request) {
            rc = flux_respond_error (s->h,
                                     request,
                                     p->failed_errno,
                                     errmsg);
            /* Clear waitable flag since a response of final state has
             * been sent to client.
             */
            clear_waitable (p);
        }
        proc_delete (s, p); // N.B. proc_delete preserves errno
    } else {
        errno = EPROTO;
        llog_error (s, "subprocess entered illegal state %d", state);
        goto error;
    }
    if (rc < 0) {
        llog_error (s,
                    "error responding to %s.exec request: %s",
                    s->service_name,
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
                    "error responding to %s.exec request: %s",
                    s->service_name,
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

    if (!p->bg) {
        if (len) {
            if (proc_output (p, stream, s, request, buf, len, false) < 0)
                goto error;
        }
        else {
            if (proc_output (p, stream, s, request, NULL, 0, true) < 0)
                goto error;
        }
    }
    else if (len) {
        /* background process: log output (ignore EOF) */

        const char *command = basename_simple (flux_cmd_arg (p->cmd, 0));
        if (streq (stream, "stderr"))
            llog_error (s, "%s[%d]: %s", command, (int)p->pid, buf);
        else
            llog_info (s, "%s[%d]: %s", command, (int)p->pid, buf);
    }
    return;

error:
    proc_internal_fatal (p);
}

static void proc_credit_cb (flux_subprocess_t *p, const char *stream, int bytes)
{
    subprocess_server_t *s = flux_subprocess_aux_get (p, srvkey);
    const flux_msg_t *request = flux_subprocess_aux_get (p, msgkey);

    if (flux_respond_pack (s->h,
                           request,
                           "{s:s s:{s:i}}",
                           "type", "add-credit",
                           "channels",
                             stream, bytes) < 0) {
        llog_error (s,
                    "error responding to %s.exec request: %s",
                    s->service_name,
                    strerror (errno));
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
        .on_credit = proc_credit_cb,
    };
    char **env = NULL;
    const char *errmsg = NULL;
    flux_error_t error;
    int flags;
    int local_flags = 0;
    const char *label;

    /* Per RFC 42, non-streaming request runs process in background:
     */
    bool background = !flux_msg_is_streaming (msg);

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:i s?i}",
                             "cmd", &cmd_obj,
                             "flags", &flags,
                             "local_flags", &local_flags) < 0)
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
    if (!background && !(flags & SUBPROCESS_REXEC_STDOUT))
        ops.on_stdout = NULL;
    if (!background && !(flags & SUBPROCESS_REXEC_STDERR))
        ops.on_stderr = NULL;
    if (!(flags & SUBPROCESS_REXEC_WRITE_CREDIT))
        ops.on_credit = NULL;

    if (!(cmd = cmd_fromjson (cmd_obj, NULL))) {
        errmsg = "error parsing command string";
        goto error;
    }

    if ((label = flux_cmd_get_label (cmd))
        && proc_find_bylabel (s, label)) {
        errmsg = "command label is not unique";
        goto error;
    }
    if (!flux_cmd_argc (cmd)) {
        errno = EPROTO;
        errmsg = "command string is empty";
        goto error;
    }

    if (!(env = cmd_env_expand (cmd))) {
        errmsg = "could not expand command environment";
        goto error;
    }
    /* If no environment is set in the command object, use the local server
     * environment.
     */
    if (env[0] == NULL) {
        if (flux_cmd_env_replace (cmd, environ) < 0) {
            errmsg = "error setting up command environment";
            goto error;
        }
    }
    /* Ensure FLUX_URI points to the local broker (overwrite).
     */
    if (flux_cmd_setenvf (cmd, 1, "FLUX_URI", "%s", s->local_uri) < 0) {
        errmsg = "error overriding FLUX_URI";
        goto error;
    }
    /* Never propagate some variables to processes
     * started from a subprocess server.
     */
    flux_cmd_unsetenv (cmd, "FLUX_PROXY_REMOTE");
    flux_cmd_unsetenv (cmd, "NOTIFY_SOCKET"); // see sd_notify(3)

    if (!(p = flux_local_exec_ex (flux_get_reactor (s->h),
                                  local_flags,
                                  cmd,
                                  &ops,
                                  NULL,
                                  s->llog,
                                  s->llog_data))) {
        errprintf (&error, "error launching process: %s", strerror (errno));
        errmsg = error.text;
        goto error;
    }

    p->bg = background;

    /* Waitable flag only allowed in background mode:
     */
    if (flags & SUBPROCESS_REXEC_WAITABLE) {
        if (!p->bg) {
            errmsg = "waitable flag only supported in background mode";
            errno = EINVAL;
            goto error;
        }
        p->waitable = true;
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
                    "error responding to %s.exec request: %s",
                    s->service_name,
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
    int matchtag;
    json_t *io = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s:o }",
                             "matchtag", &matchtag,
                             "io", &io) < 0
        || iodecode (io, &stream, NULL, &data, &len, &eof) < 0) {
        llog_error (s,
                    "Error decoding %s.write request: %s",
                    s->service_name,
                    strerror (errno));
        goto out;
    }
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        llog_error (s, "%s.write: %s", s->service_name, error.text);
        goto out;
    }

    /* If the subprocess can't be found or is no longer running, just silently
     * drop the data. This is expected if tasks are killed or exit with data
     * in flight, and is not necessarily an error, and can be common enough
     * that the log messages end up being a nuisance.
     */
    if (!(p = proc_find_byclient (s, msg))
        || p->state == FLUX_SUBPROCESS_FAILED
        || p->state == FLUX_SUBPROCESS_EXITED)
        goto out;

    if (data && len) {
        int rc = flux_subprocess_write (p, stream, data, len);
        if (rc < 0) {
            llog_error (s,
                        "Error writing %d bytes to subprocess %s",
                        len,
                        stream);
            goto error;
        }
    }
    if (eof) {
        if (flux_subprocess_close (p, stream) < 0) {
            llog_error (s, "Error writing EOF to subprocess %s", stream);
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

static flux_subprocess_t *proc_find (subprocess_server_t *s,
                                     pid_t pid,
                                     const char *label,
                                     flux_error_t *errp)
{
    flux_subprocess_t *p = NULL;
    if (label) {
        if (!(p = proc_find_bylabel (s, label))) {
            errprintf (errp,
                       "label %s does not belong to any subprocess",
                       label);
        }
    }
    else if (!(p = proc_find_bypid (s, pid))) {
        errprintf (errp, "pid %d does not belong to any subprocess", pid);
    }
    return p;
}

static void server_kill_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    subprocess_server_t *s = arg;
    pid_t pid;
    int signum;
    const char *label = NULL;
    flux_error_t error;
    const char *errmsg = NULL;
    flux_subprocess_t *p;
    flux_future_t *f = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s:i s?s }",
                             "pid", &pid,
                             "signum", &signum,
                             "label", &label) < 0)
        goto error;
    if (s->auth_cb && (*s->auth_cb) (msg, s->arg, &error) < 0) {
        errmsg = error.text;
        errno = EPERM;
        goto error;
    }
    if (!(p = proc_find (s, pid, label, &error))) {
        errmsg = error.text;
        goto error;
    }
    if (!(f = flux_subprocess_kill (p, signum))
        || flux_future_get (f, NULL) < 0) { // will never block
        errprintf (&error, "%s", future_strerror (f, errno));
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0) {
        llog_error (s,
                    "error responding to %s.kill request: %s",
                    s->service_name,
                    strerror (errno));
    }
    flux_future_destroy (f);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        llog_error (s,
                    "error responding to %s.kill request: %s",
                    s->service_name,
                    strerror (errno));
    }
    flux_future_destroy (f);
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
    const char *label;
    const char *state;

    if (!(cmd = flux_subprocess_get_cmd (p)))
        return NULL;
    label = flux_cmd_get_label (cmd);
    state = flux_subprocess_active (p) ? "R" : "Z";
    if (!(info = json_pack ("{s:i s:s s:s s:s}",
                            "pid", flux_subprocess_pid (p),
                            "cmd", flux_cmd_arg (cmd, 0),
                            "label", label ? label : "",
                            "state", state))) {
        errno = ENOMEM;
        return NULL;
    }
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
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:o}",
                           "rank", s->rank,
                           "procs", procs) < 0) {
        llog_error (s,
                    "error responding to %s.list request: %s",
                    s->service_name,
                    strerror (errno));
    }
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0) {
        llog_error (s,
                    "error responding to %s.list request: %s",
                    s->service_name,
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
            if (!p->bg && sender && streq (uuid, sender))
                server_kill (p, SIGKILL);
            if (p->waiter
                && streq (flux_msg_route_first (p->waiter), sender)) {
                flux_msg_decref (p->waiter);
                p->waiter = NULL;
            }
            p = zlistx_next (s->subprocesses);
        }
    }
}

static void server_wait_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    subprocess_server_t *s = arg;
    flux_subprocess_t *p;
    flux_error_t error;
    const char *errmsg = NULL;
    pid_t pid;
    const char *label = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{ s:i s?s }",
                             "pid", &pid,
                             "label", &label) < 0)
        goto error;
    if (!(p = proc_find (s, pid, label, &error))) {
        errmsg = error.text;
        goto error;
    }
    if (!is_waitable (p)) {
        errmsg = "process is not waitable";
        errno = EINVAL;
        goto error;
    }
    if (p->waiter) {
        errmsg = "process is already being waited on";
        errno = EINVAL;
        goto error;
    }
    p->waiter = flux_msg_incref (msg);

    /* If process is complete, proc_delete() notifies p->waiter of final
     * status and removes process from s->processes if wait response was
     * sent. Otherwise, does nothing.
     */
    proc_delete (s, p);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        llog_error (s,
                    "error responding to %s.wait request: %s",
                    s->service_name,
                    strerror (errno));
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
      "wait",
      server_wait_cb,
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

static void server_purge_zombies (subprocess_server_t *s)
{
    flux_subprocess_t *p;

    p = zlistx_first (s->subprocesses);
    while (p) {
        if (!flux_subprocess_active (p)) {
            clear_waitable (p);
            proc_delete (s, p);
        }
        p = zlistx_next (s->subprocesses);
    }
}

static int server_killall (subprocess_server_t *s, int signum)
{
    flux_subprocess_t *p;

    /* Delete zombies so they do not hold up shutdown
     */
    server_purge_zombies (s);

    p = zlistx_first (s->subprocesses);
    while (p) {
        clear_waitable (p);
        if (flux_subprocess_active (p))
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
        zhashx_destroy (&s->labels);
        free (s->service_name);
        free (s->local_uri);
        if (s->has_sigchld_ctx)
            sigchld_finalize ();
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

    if (!(s->subprocesses = zlistx_new ())
        || !(s->labels = zhashx_new ()))
        goto error;
    zlistx_set_destructor (s->subprocesses, proc_destructor);
    if (!(s->service_name = strdup (service_name)))
        goto error;
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

    /* Avoid unnecessary on-demand create/destroy of SIGCHLD handler + hash.
     */
    if (sigchld_initialize (flux_get_reactor (h)) < 0)
        goto error;
    s->has_sigchld_ctx = true;
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

static void shutdown_future_invalidate (void *arg)
{
    subprocess_server_t *s = arg;
    s->shutdown = NULL;
}

flux_future_t *subprocess_server_shutdown (subprocess_server_t *s, int signum)
{
    flux_future_t *f;

    if (!s) {
        errno = EINVAL;
        return NULL;
    }

    /* Create a future that will be fulfilled when all server processes
     * have exited. Arrange to have this future invalidated if it is destroyed
     * by the client so their callback doesn't get unnecessarily called
     * and also to allow this function to be called multiple times.
     */
    if (!(f = flux_future_create (NULL, NULL))
        || flux_future_aux_set (f, NULL, s, shutdown_future_invalidate) < 0) {
        flux_future_destroy (f);
        return NULL;
    }
    flux_future_set_reactor (f, flux_get_reactor (s->h));
    flux_future_set_flux (f, s->h);
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
