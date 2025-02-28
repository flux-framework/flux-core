/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sdexec.c - run subprocesses under systemd as transient units
 *
 * Configuration:
 *  [systemd]
 *  sdexec-debug = true   # enables debug logging
 *  enable = true         # enables auto loading by rc script
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <jansson.h>
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <flux/core.h>
#if HAVE_FLUX_SECURITY
#include <flux/security/version.h>
#endif


#include "src/common/libsubprocess/client.h"
#include "src/common/libioencode/ioencode.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/parse_size.h"
#include "ccan/str/str.h"

#include "src/common/libsdexec/stop.h"
#include "src/common/libsdexec/start.h"
#include "src/common/libsdexec/channel.h"
#include "src/common/libsdexec/unit.h"
#include "src/common/libsdexec/property.h"

#define MODULE_NAME "sdexec"

struct sdexec_ctx {
    flux_t *h;
    uint32_t rank;
    char *local_uri;
    flux_msg_handler_t **handlers;
    struct flux_msglist *requests; // each exec request "owns" an sdproc
    struct flux_msglist *kills;
};

enum stop_timer_state {
    STOP_TIMER_OFF,
    STOP_TIMER_SIGKILL,
    STOP_TIMER_ABANDON,
};

struct stop_timer {
    flux_watcher_t *timer;
    enum stop_timer_state state;
    int kill_signal;
    int timeout_sec;
    bool timed_out;
};

struct sdproc {
    const flux_msg_t *msg;
    json_t *cmd;
    int flags;
    flux_future_t *f_watch;
    flux_future_t *f_start;
    flux_future_t *f_stop;
    struct unit *unit;
    struct flux_msglist *write_requests;
    struct channel *in;
    struct channel *out;
    struct channel *err;
    uint8_t started_response_sent:1;
    uint8_t finished_response_sent:1;
    uint8_t out_eof_sent:1;
    uint8_t err_eof_sent:1;

    struct stop_timer stop;

    int errnum;
    const char *errstr;
    flux_error_t error;

    struct sdexec_ctx *ctx;
};

static const int default_stop_timeout_sec = -1; // disabled by default

static bool sdexec_debug;

static __attribute__ ((format (printf, 2, 3)))
void sdexec_log_debug (flux_t *h, const char *fmt, ...)
{
    if (sdexec_debug) {
        va_list ap;

        va_start (ap, fmt);
        flux_vlog (h, LOG_DEBUG, fmt, ap);
        va_end (ap);
    }
}

static void delete_message (struct flux_msglist *msglist,
                            const flux_msg_t *msg)
{
    const flux_msg_t *m;

    m = flux_msglist_first (msglist);
    while (m) {
        if (msg == m) {
            flux_msglist_delete (msglist);
            return;
        }
        m = flux_msglist_next (msglist);
    }
}

static const flux_msg_t *lookup_message_bypid (struct flux_msglist *msglist,
                                               pid_t pid)
{
    const flux_msg_t *m;

    m = flux_msglist_first (msglist);
    while (m) {
        struct sdproc *proc = flux_msg_aux_get (m, "sdproc");
        if (sdexec_unit_pid (proc->unit) == pid)
            return m;
        m = flux_msglist_next (msglist);
    }
    return NULL;
}

static const flux_msg_t *lookup_message_byaux (struct flux_msglist *msglist,
                                               const char *name,
                                               void *value)
{
    const flux_msg_t *m;

    m = flux_msglist_first (msglist);
    while (m) {
        if (flux_msg_aux_get (m, name) == value)
            return m;
        m = flux_msglist_next (msglist);
    }
    return NULL;
}

/* Find an sdexec.exec message with the same sender as msg and matchtag as
 * specified in the msg matchtag field.
 * N.B. flux_cancel_match() happens to be helpful because RFC 42 subprocess
 * write works like RFC 6 cancel.
 */
static const flux_msg_t *lookup_message_byclient (struct flux_msglist *msglist,
                                                  const flux_msg_t *msg)
{
    const flux_msg_t *m;

    m = flux_msglist_first (msglist);
    while (m) {
        if (flux_cancel_match (msg, m))
            return m;
        m = flux_msglist_next (msglist);
    }
    return NULL;
}

static void exec_respond_error (struct sdproc *proc,
                                int errnum,
                                const char *errstr)
{
    if (flux_respond_error (proc->ctx->h, proc->msg, errnum, errstr) < 0)
        flux_log_error (proc->ctx->h, "error responding to exec request");
    delete_message (proc->ctx->requests, proc->msg); // destroys proc too
}

/* Send the streaming response IFF unit cleanup is complete and EOFs have
 * been sent.  Channel EOF and cleanup might(?) complete out of order so
 * call this from unit and channel callbacks.
 */
static void finalize_exec_request_if_done (struct sdproc *proc)
{
    if (proc->stop.timed_out) {
        exec_respond_error (proc,
                            EDEADLK,
                            "Processes did not respond to SIGKILL."
                            " Abandoning unit as is.");
    }
    else if (sdexec_unit_state (proc->unit) == STATE_INACTIVE
        && sdexec_unit_substate (proc->unit) == SUBSTATE_DEAD
        && (!proc->out || proc->out_eof_sent)
        && (!proc->err || proc->err_eof_sent)) {

        /* If there was an exec error, fail with ENOENT.
         * N.B. we have no way of discerning which exec(2) error occurred,
         * so guess ENOENT.  It could actually be EPERM, for example.
         */
        if (sdexec_unit_has_failed (proc->unit)) {
            flux_error_t error;
            errprintf (&error,
                       "unit process could not be started (systemd error %d)",
                       sdexec_unit_systemd_error (proc->unit));
            exec_respond_error (proc, ENOENT, error.text);
        }
        else if (!proc->started_response_sent) {
            exec_respond_error (proc,
                                EINVAL,
                                "Internal error: unfailed inactive.dead unit"
                                " never received ExecMainPID property");
        }
        else if (!proc->finished_response_sent) {
            exec_respond_error (proc,
                                EINVAL,
                                "Internal error: unfailed inactive.dead unit"
                                " never received ExecMainCode and"
                                " ExecMainStatus properties.");
        }
        else
            exec_respond_error (proc, ENODATA, NULL);
    }
}

static void stop_timer_start (struct stop_timer *stop,
                              enum stop_timer_state state)
{
    if (stop->timeout_sec < 0)
        return; // stop timer is disabled
    stop->state = state;
    flux_timer_watcher_reset (stop->timer, (double)stop->timeout_sec, 0.);
    flux_watcher_start (stop->timer);
}

static void stop_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct sdproc *proc = arg;

    switch (proc->stop.state) {
        case STOP_TIMER_SIGKILL:
            sdexec_log_debug (proc->ctx->h,
                              "%s: killing after %ds stop timeout",
                              sdexec_unit_name (proc->unit),
                              proc->stop.timeout_sec);
            flux_future_t *f = NULL;
            f = sdexec_kill_unit (proc->ctx->h,
                                  proc->ctx->rank,
                                  sdexec_unit_name (proc->unit),
                                  "main",
                                  proc->stop.kill_signal);
            flux_future_destroy (f);
            stop_timer_start (&proc->stop, STOP_TIMER_ABANDON);
            break;
        case STOP_TIMER_ABANDON:
            sdexec_log_debug (proc->ctx->h,
                              "%s: abandoning after %ds stop timeout",
                              sdexec_unit_name (proc->unit),
                              proc->stop.timeout_sec * 2);
            proc->stop.timed_out = true;
            finalize_exec_request_if_done (proc); // destroys proc
            break;
        case STOP_TIMER_OFF:
            break;
    }
}

static void stop_continuation (flux_future_t *f, void *arg)
{
    struct sdproc *proc = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        flux_log (proc->ctx->h,
                  LOG_ERR,
                  "stop %s: %s",
                  sdexec_unit_name (proc->unit),
                  future_strerror (f, errno));
    }
}

static void reset_continuation (flux_future_t *f, void *arg)
{
    struct sdproc *proc = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        flux_log (proc->ctx->h,
                  LOG_ERR,
                  "reset-failed %s: %s",
                  sdexec_unit_name (proc->unit),
                  future_strerror (f, errno));
    }
}

/* sdbus.subscribe sent a PropertiesChanged response for a particular unit.
 * Advance the proc->unit state accordingly and send exec responses as needed.
 * call finalize_exec_request_if_done() in case this update is the last thing
 * the exec request was waiting for.
 */
static void property_changed_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct sdproc *proc = arg;
    json_t *properties;

    if (!(properties = sdexec_property_changed_dict (f))) {
        exec_respond_error (proc, errno, future_strerror (f, errno));
        return;
    }
    if (!sdexec_unit_update (proc->unit, properties)) {
        flux_future_reset (f);
        return;
    }

    sdexec_log_debug (h,
                      "%s: %s.%s",
                      sdexec_unit_name (proc->unit),
                      sdexec_statetostr (sdexec_unit_state (proc->unit)),
                      sdexec_substatetostr (sdexec_unit_substate (proc->unit)));

    /* The started response must be the first response to an exec request,
     * so channel output may begin after this.
     * If there is an exec error, "started" should not be sent.
     */
    if (!proc->started_response_sent) {
        if (sdexec_unit_has_started (proc->unit)) {
            if (flux_respond_pack (h,
                                   proc->msg,
                                   "{s:s s:I}",
                                   "type", "started",
                                   "pid", sdexec_unit_pid (proc->unit)) < 0)
                flux_log_error (h, "error responding to exec request");
            proc->started_response_sent = 1;
            sdexec_channel_start_output (proc->out);
            sdexec_channel_start_output (proc->err);
        }
    }
    /* The finished response is sent when wait status is available.
     * If there was an exec error, "finished" should not be sent.
     */
    if (!proc->finished_response_sent) {
        if (sdexec_unit_has_finished (proc->unit)) {
            if (flux_respond_pack (h,
                                   proc->msg,
                                   "{s:s s:i}",
                                   "type", "finished",
                                   "status",
                                   sdexec_unit_wait_status (proc->unit)) < 0)
                flux_log_error (h, "error responding to exec request");
            proc->finished_response_sent = 1;
        }
    }
    /* If the unit reaches active.exited call StopUnit to cause stdout
     * and stderr to reach eof, and the unit to transition to inactive.dead.
     */
    if (sdexec_unit_state (proc->unit) == STATE_ACTIVE
        && sdexec_unit_substate (proc->unit) == SUBSTATE_EXITED
        && proc->finished_response_sent) {

        if (!proc->f_stop) {
            flux_future_t *f2;
            sdexec_log_debug (h, "stop %s", sdexec_unit_name (proc->unit));
            if (!(f2 = sdexec_stop_unit (h,
                                         proc->ctx->rank,
                                         sdexec_unit_name (proc->unit),
                                         "fail"))
                || flux_future_then (f2, -1, stop_continuation, proc) < 0) {
                flux_log_error (h, "error initiating unit stop");
                flux_future_destroy (f2);
                f2 = NULL;
            }
            proc->f_stop = f2;
        }
    }
    /* If the unit reaches deactivating state, start the stop timer.
     * The stop timer is necessary to help imp-shell make progress if the
     * shell has exited but processes remain in the cgroup.
     */
    if (sdexec_unit_state (proc->unit) == STATE_DEACTIVATING) {
        stop_timer_start (&proc->stop, STOP_TIMER_SIGKILL);
    }
    /* If the unit reaches failed.failed call ResetFailedUnit to cause stdout
     * and stderr to reach eof, and the unit to transition to inactive.dead.
     * We can land here for both a child failure and an exec failure.
     * Start channel output here in case of the latter so it can be finalized.
     */
    if (sdexec_unit_state (proc->unit) == STATE_FAILED
        && sdexec_unit_substate (proc->unit) == SUBSTATE_FAILED) {

        sdexec_channel_start_output (proc->out);
        sdexec_channel_start_output (proc->err);

        if (!proc->f_stop) {
            flux_future_t *f2;
            sdexec_log_debug (h,
                              "reset-failed %s",
                              sdexec_unit_name (proc->unit));
            if (!(f2 = sdexec_reset_failed_unit (h,
                                                 proc->ctx->rank,
                                                 sdexec_unit_name (proc->unit)))
                || flux_future_then (f2, -1, reset_continuation, proc) < 0) {
                flux_log_error (h, "error initiating unit reset");
                flux_future_destroy (f2);
                f2 = NULL;
            }
            proc->f_stop = f2;
        }
    }
    flux_future_reset (f);
    /* Conditionally send the final RPC response.
     */
    finalize_exec_request_if_done (proc);
}

/* StartTransientUnit reply does not normally generate a sdexec.exec response,
 * unless it fails.  Streaming responses continue as property change updates
 * are received from sdbus.
 */
static void start_continuation (flux_future_t *f, void *arg)
{
    const flux_msg_t *msg = flux_future_aux_get (f, "request");
    struct sdproc *proc = flux_msg_aux_get (msg, "sdproc");
    struct sdexec_ctx *ctx = arg;

    if (sdexec_start_transient_unit_get (f, NULL) < 0)
        goto error;
    /* Now that systemd has acknowledged the StartTransientUnit request, close
     * the systemd end of any channel(s).  The assumption is that systemd has
     * received its fd and has already called dup(2) on it.
     */
    sdexec_channel_close_fd (proc->in);
    sdexec_channel_close_fd (proc->out);
    sdexec_channel_close_fd (proc->err);
    /* Now that stdin is ready, re-queue any messages write_cb() left in
     * proc->write_requests.  Push these messages to the front of the flux_t
     * queue so that they come before unprocessed writes, if any.
     */
    if (proc->write_requests) {
        const flux_msg_t *request;
        while ((request = flux_msglist_pop (proc->write_requests))) {
            int rc = flux_requeue (ctx->h, request, FLUX_RQ_HEAD);
            flux_msg_decref (request);
            if (rc < 0) {
                flux_log_error (ctx->h, "error requeuing early sdexec.write");
                break;
            }
        }
    }
    return;
error:
    if (flux_respond_error (ctx->h, msg, errno, future_strerror (f, errno)))
        flux_log_error (ctx->h, "error responding to exec request");
    delete_message (ctx->requests, msg);
}

/* Log an error receiving data from unit stdout or stderr.  channel_cb will
 * be called with an EOF after this callback returns.
 */
static void cherror_cb (struct channel *ch, flux_error_t *error, void *arg)
{
    struct sdproc *proc = arg;
    flux_t *h = proc->ctx->h;

    flux_log (h, LOG_ERR, "%s: %s", sdexec_channel_get_name (ch), error->text);
}

/* Receive some data from unit stdout or stderr and forward it as an
 * exec response.  In case this was the last thing the exec request was
 * waiting to receive (e.g. a final EOF), call finalize_exec_request_if_done()
 * to take care of that if needed.
 */
static void channel_cb (struct channel *ch, json_t *io, void *arg)
{
    struct sdproc *proc = arg;
    flux_t *h = proc->ctx->h;

    if (flux_respond_pack (h,
                           proc->msg,
                           "{s:s s:i s:O}",
                           "type", "output",
                           "pid", sdexec_unit_pid (proc->unit),
                           "io", io) < 0)
        flux_log_error (h, "error responding to exec request");

    const char *stream;
    bool eof;
    if (iodecode (io, &stream, NULL, NULL, NULL, &eof) == 0 && eof == true) {
        if (streq (stream, "stdout"))
            proc->out_eof_sent = true;
        else if (streq (stream, "stderr"))
            proc->err_eof_sent = true;
    }
    finalize_exec_request_if_done (proc);
}

/* Since an sdproc is attached to each exec message's aux container, this
 * destructor is typically called when an exec request is destroyed, e.g.
 * after unit reaping is complete and the exec client has been sent ENODATA
 * or another error response.  This ends the sdbus.subscribe request for
 * property updates on this unit.  The subscribe future is destroyed here;
 * we do not wait for the ENODATA response.
 */
static void sdproc_destroy (struct sdproc *proc)
{
    if (proc) {
        int saved_errno = errno;
        sdexec_channel_destroy (proc->in);
        sdexec_channel_destroy (proc->out);
        sdexec_channel_destroy (proc->err);
        if (proc->f_watch) {
            flux_future_t *f;
            sdexec_log_debug (proc->ctx->h,
                              "unwatch %s",
                              sdexec_unit_name (proc->unit));
            f = flux_rpc_pack (proc->ctx->h,
                               "sdbus.subscribe-cancel",
                               proc->ctx->rank,
                               FLUX_RPC_NORESPONSE,
                               "{s:i}",
                               "matchtag",
                               flux_rpc_get_matchtag (proc->f_watch));
            flux_future_destroy (f);
            flux_future_destroy (proc->f_watch);
        }
        flux_future_destroy (proc->f_start);
        flux_future_destroy (proc->f_stop);
        sdexec_unit_destroy (proc->unit);
        flux_watcher_destroy (proc->stop.timer);
        json_decref (proc->cmd);
        flux_msglist_destroy (proc->write_requests);
        free (proc);
        errno = saved_errno;
    }
}

/* Unset key 'k' in the dictionary named 'name'.
 */
static void unset_dict (json_t *o, const char *name, const char *k)
{
    json_t *dict;
    if ((dict = json_object_get (o, name)))
        json_object_del (dict, k);
}

/* Set a key 'k', value 'v' pair in the dictionary named 'name'.
 * The dictionary is created if it does not exist.
 * If key is already set, the previous value is overwritten.
 */
static int set_dict (json_t *o,
                     const char *name,
                     const char *k,
                     const char *v)
{
    json_t *dict;
    json_t *vo;

    if (!(dict = json_object_get (o, name))) {
        if (!(dict = json_object ())
            || json_object_set_new (o, name, dict) < 0) {
            json_decref (dict);
            goto nomem;
        }
    }
    if (!(vo = json_string (v))
        || json_object_set_new (dict, k, vo) < 0) {
        json_decref (vo);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

/* Look up key 'k' in dictionary named 'name' and assign value to 'vp'.
 */
static int get_dict (json_t *o,
                     const char *name,
                     const char *k,
                     const char **vp)
{
    const char *v;
    if (json_unpack (o, "{s:{s:s}}", name, k, &v) < 0) {
        errno = ENOENT;
        return -1;
    }
    *vp = v;
    return 0;
}

static int get_dict_int (json_t *o,
                         const char *name,
                         const char *k,
                         int *vp)
{
    const char *v;
    if (get_dict (o, name, k, &v) < 0)
        return -1;

    char *endptr;
    errno = 0;
    int i = strtol (v, &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *vp = i;
    return 0;
}

static int get_stream_bufsize (json_t *cmd,
                               const char *stream,
                               size_t *vp)
{
    char key[64];
    const char *val;
    uint64_t result;

    snprintf (key, sizeof (key), "%s_BUFSIZE", stream);
    if (get_dict (cmd, "opts", key, &val) < 0) {
        *vp = 0;
        return 0;
    }
    if (parse_size (val, &result) < 0 || result > SIZE_MAX)
        return -1;
    *vp = result;
    return 0;
}

static int get_stream_line_buffer (json_t *cmd,
                                   const char *stream,
                                   bool *vp,
                                   bool default_value)
{
    char key[64];
    const char *val;

    snprintf (key, sizeof (key), "%s_LINE_BUFFER", stream);
    if (get_dict (cmd, "opts", key, &val) < 0) {
        *vp = default_value;
        return 0;
    }
    if (!strcasecmp (val, "false"))
        *vp = false;
    else if (!strcasecmp (val, "true"))
        *vp = true;
    else
        return -1;
    return 0;
}

static struct channel *create_out_channel (flux_t *h,
                                           json_t *cmd,
                                           const char *stream,
                                           void *arg)
{
    bool linebuf;
    size_t bufsize;

    if (get_stream_line_buffer (cmd, stream, &linebuf, true) < 0
        || get_stream_bufsize (cmd, stream, &bufsize) < 0)
        return NULL;
    return sdexec_channel_create_output (h,
                                         stream,
                                         bufsize,
                                         linebuf ? CHANNEL_LINEBUF : 0,
                                         channel_cb,
                                         cherror_cb,
                                         arg);
}

static struct sdproc *sdproc_create (struct sdexec_ctx *ctx,
                                     json_t *cmd,
                                     int flags)
{
    struct sdproc *proc;
    const int valid_flags = SUBPROCESS_REXEC_STDOUT
        | SUBPROCESS_REXEC_STDERR
        | SUBPROCESS_REXEC_CHANNEL;
    const char *name;
    char *tmp = NULL;
    flux_reactor_t *reactor = flux_get_reactor (ctx->h);

    if ((flags & ~valid_flags) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(proc = calloc (1, sizeof (*proc))))
        return NULL;
    proc->ctx = ctx;
    proc->flags = flags;
    if (!(proc->stop.timer = flux_timer_watcher_create (reactor,
                                                        0,
                                                        0,
                                                        stop_timer_cb,
                                                        proc)))
        goto error;
    if (!(proc->cmd = json_deep_copy (cmd))) {
        errno = ENOMEM;
        goto error;
    }
    /* Enable the stop timer by setting the SDEXEC_STOP_TIMER_SEC option to
     * a value in seconds.  The stop timer is disabled by default.
     * sOptionally set SDEXEC_STOP_TIMER_SIGNAL to a numerical signal
     * value to use instead of SIGKILL.
     */
    if (get_dict_int (proc->cmd,
                      "opts",
                      "SDEXEC_STOP_TIMER_SEC",
                      &proc->stop.timeout_sec) < 0)
        proc->stop.timeout_sec = default_stop_timeout_sec;
    if (get_dict_int (proc->cmd,
                      "opts",
                      "SDEXEC_STOP_TIMER_SIGNAL",
                      &proc->stop.kill_signal) < 0)
        proc->stop.kill_signal = SIGKILL;
    /* Set SDEXEC_NAME for sdexec_start_transient_unit().
     * If unset, use a truncated uuid as the name.
     */
    if (get_dict (proc->cmd, "opts", "SDEXEC_NAME", &name) < 0) {
        uuid_t uuid;
        char uuid_str[UUID_STR_LEN];

        uuid_generate (uuid);
        uuid_unparse (uuid, uuid_str);
        uuid_str[13] = '\0'; // plenty of uniqueness
        if (asprintf (&tmp, "%s.service", uuid_str) < 0
            || set_dict (proc->cmd, "opts", "SDEXEC_NAME", tmp) < 0)
            goto error;
        name = tmp;
    }
    if (!(proc->unit = sdexec_unit_create (name)))
        goto error;
    /* Ensure that FLUX_URI refers to the local broker.
     */
    if (set_dict (proc->cmd, "env", "FLUX_URI", ctx->local_uri) < 0)
        goto error;
    unset_dict (proc->cmd, "env", "NOTIFY_SOCKET"); // see sd_notify(3)
    /* Create channels for stdio as required by flags.
     */
    if (!(proc->in = sdexec_channel_create_input (ctx->h, "stdin")))
        goto error;
    if ((flags & SUBPROCESS_REXEC_STDOUT)) {
        if (!(proc->out = create_out_channel (ctx->h,
                                              proc->cmd,
                                              "stdout",
                                              proc)))
            goto error;
    }
    if ((flags & SUBPROCESS_REXEC_STDERR)) {
        if (!(proc->err = create_out_channel (ctx->h,
                                              proc->cmd,
                                              "stderr",
                                              proc)))
            goto error;
    }
    free (tmp);
    return proc;
error:
    ERRNO_SAFE_WRAP (free, tmp);
    sdproc_destroy (proc);
    return NULL;
}

static int authorize_request (const flux_msg_t *msg,
                              uint32_t rank,
                              flux_error_t *error)
{
    if (rank != 0 || flux_msg_is_local (msg))
        return 0;
    errprintf (error, "Remote sdexec requests are not allowed on rank 0");
    errno = EPERM;
    return -1;
}

/* Start a process as a systemd transient unit.  This is a streaming request.
 * It triggers two sdbus RPCs:
 * 1) sdbus.subscribe (streaming) for updates to this unit's properties
 * 2) sdbus.call StartTransientUnit to launch the transient unit.
 * Responses to those are handled in property_changed_continuation()
 * and start_continuation().
 */
static void exec_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct sdexec_ctx *ctx = arg;
    json_t *cmd;
    int flags;
    flux_error_t error;
    const char *errstr = NULL;
    struct sdproc *proc;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:o s:i}",
                             "cmd", &cmd,
                             "flags", &flags) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errstr = "exec request is missing STREAMING flag";
        errno = EPROTO;
        goto error;
    }
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if ((flags & SUBPROCESS_REXEC_CHANNEL)) {
        errstr = "subprocess auxiliary channels are not supported yet";
        errno = EINVAL;
        goto error;
    }
    if (!(proc = sdproc_create (ctx, cmd, flags))
        || flux_msg_aux_set (msg,
                             "sdproc",
                             proc,
                             (flux_free_f)sdproc_destroy) < 0) {
        sdproc_destroy (proc);
        goto error;
    }
    proc->msg = msg;
    sdexec_log_debug (h, "watch %s", sdexec_unit_name (proc->unit));
    if (!(proc->f_watch = sdexec_property_changed (h,
                                               ctx->rank,
                                               sdexec_unit_path (proc->unit)))
        || flux_future_then (proc->f_watch,
                             -1,
                             property_changed_continuation,
                             proc) < 0)
        goto error;
    sdexec_log_debug (h, "start %s", sdexec_unit_name (proc->unit));
    if (!(proc->f_start = sdexec_start_transient_unit (h,
                                             ctx->rank,
                                             "fail", // mode
                                             proc->cmd,
                                             sdexec_channel_get_fd (proc->in),
                                             sdexec_channel_get_fd (proc->out),
                                             sdexec_channel_get_fd (proc->err),
                                             &error))) {
        errstr = error.text;
        goto error;
    }
    if (flux_future_then (proc->f_start, -1, start_continuation, ctx) < 0
        || flux_future_aux_set (proc->f_start,
                                "request",
                                (void *)msg,
                                NULL) < 0)
        goto error;
    if (flux_msglist_append (ctx->requests, msg) < 0)
        goto error;
    return; // response occurs later
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to exec request");
}

/* Send some data to stdin of a unit started with sdexec.exec.
 * The unit is looked up by pid. This request is "fire and forget"
 * (no response) per libsubprocess protocol.
 */
static void write_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct sdexec_ctx *ctx = arg;
    int matchtag;
    json_t *io;
    const flux_msg_t *exec_request;
    flux_error_t error;
    struct sdproc *proc;
    const char *stream;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:o}",
                             "matchtag", &matchtag,
                             "io", &io) < 0) {
        flux_log_error (h, "error decoding write request");
        return;
    }
    if (!flux_msg_is_noresponse (msg)) {
        flux_log (h, LOG_ERR, "write request is missing NORESPONSE flag");
        return;
    }
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        flux_log_error (h, "%s", error.text);
        return;
    }
    if (!(exec_request = lookup_message_byclient (ctx->requests, msg))
        || !(proc = flux_msg_aux_get (exec_request, "sdproc"))) {
        flux_log (h, LOG_ERR, "sdexec.write: subprocess no longer exists");
        return;
    }
    /* If the systemd unit has not started yet, enqueue the write request for
     * later processing in start_continuation().  We can tell that it hasn't
     * started if start_continuation() has not yet handed the stdin channel
     * file descriptor over to systemd by calling the close function.
     */
    if (sdexec_channel_get_fd (proc->in) != -1) { // not yet claimed by systemd
        if (!proc->write_requests) {
            if (!(proc->write_requests = flux_msglist_create ())) {
                flux_log_error (h, "sdexec.write: error creating write queue");
                return;
            }
        }
        if (flux_msglist_push (proc->write_requests, msg) < 0)
            flux_log_error (h, "sdexec.write: error enqueueing write request");
        return;
    }
    if (iodecode (io, &stream, NULL, NULL, NULL, NULL) == 0
        && !streq (stream, "stdin")) {
        flux_log (h, LOG_ERR, "sdexec.write: %s is an invalid stream", stream);
        return;
    }
    if (sdexec_channel_write (proc->in, io) < 0) {
        flux_log_error (h, "sdexec.write %s", stream);
        return;
    }
}

static void kill_continuation (flux_future_t *f, void *arg)
{
    struct sdexec_ctx *ctx = arg;
    flux_t *h = ctx->h;
    const flux_msg_t *msg = lookup_message_byaux (ctx->kills, "kill", f);
    int rc;

    if (flux_rpc_get (f, NULL) < 0)
        rc = flux_respond_error (h, msg, errno, future_strerror (f, errno));
    else
        rc = flux_respond (h, msg, NULL) ;
    if (rc < 0)
        flux_log_error (h, "error responding to kill request");
    flux_msglist_delete (ctx->kills); // destroys msg and f
}

/* Handle a kill by pid request.  This does not work on arbitrary pids,
 * only the pids of units started with sdexec.exec since the sdexec module
 * was loaded.  Since this sends an sdbus RPC, the response is handled in
 * kill_continuation() when the sdbus response is received.
 */
static void kill_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct sdexec_ctx *ctx = arg;
    pid_t pid;
    int signum;
    const flux_msg_t *exec_request;
    struct sdproc *proc;
    flux_error_t error;
    const char *errstr = NULL;
    flux_future_t *f;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:i}",
                             "pid", &pid,
                             "signum", &signum) < 0)
        goto error;
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (!(exec_request = lookup_message_bypid (ctx->requests, pid))
        || !(proc = flux_msg_aux_get (exec_request, "sdproc"))) {
        errprintf (&error, "kill pid=%d not found", pid);
        errstr = error.text;
        errno = ESRCH;
        goto error;
    }
    sdexec_log_debug (h,
                      "kill main %s (signal %d)",
                      sdexec_unit_name (proc->unit),
                      signum);
    if (!(f = sdexec_kill_unit (h,
                                ctx->rank,
                                sdexec_unit_name (proc->unit),
                                "main",
                                signum))
        || flux_future_then (f, -1, kill_continuation, ctx) < 0
        || flux_msg_aux_set (msg,
                             "kill",
                             f,
                             (flux_free_f)flux_future_destroy) < 0) {
        flux_future_destroy (f);
        errstr = "error sending KillUnit request";
        goto error;
    }
    // kill_continuation will respond
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to kill request");
}

/* Handle an sdexec.list request.
 * At this time, this RPC is only used in test and the returned data
 * is sparse.  It could be expanded later if needed.
 */
static void list_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct sdexec_ctx *ctx = arg;
    flux_error_t error;
    const char *errstr = NULL;
    json_t *procs = NULL;
    const flux_msg_t *req;

    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (!(procs = json_array ()))
        goto nomem;
    req = flux_msglist_first (ctx->requests);
    while (req) {
        struct sdproc *proc;
        const char *arg0;
        json_t *o;
        if ((proc = flux_msg_aux_get (req, "sdproc"))
            && json_unpack (proc->cmd, "{s:[s]}", "cmdline", &arg0) == 0
            && (o = json_pack ("{s:i s:s}",
                               "pid", sdexec_unit_pid (proc->unit),
                               "cmd", arg0))) {
            if (json_array_append_new (procs, o) < 0) {
                json_decref (o);
                goto nomem;
            }
        }
        req = flux_msglist_next (ctx->requests);
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:O}",
                           "rank", ctx->rank,
                           "procs", procs) < 0)
        flux_log_error (h, "error responding to list request");
    json_decref (procs);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to list request");
    json_decref (procs);
}

/* Make a string like "inactive.dead" in buf[size]
 */
static int get_statestr (char *buf, size_t size, struct unit *unit)
{
    const char *s = sdexec_statetostr (sdexec_unit_state (unit));
    const char *ss = sdexec_substatetostr (sdexec_unit_substate (unit));
    if (snprintf (buf, size, "%s.%s", s, ss) >= size) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static json_t *get_proc_stats (struct sdproc *proc)
{
    json_t *o;
    char statebuf[64];
    json_t *in_stats;
    json_t *out_stats;
    json_t *err_stats;

    if (!proc->unit
        || get_statestr (statebuf, sizeof (statebuf), proc->unit) < 0)
        return NULL;
    in_stats = sdexec_channel_get_stats (proc->in);
    out_stats = sdexec_channel_get_stats (proc->out);
    err_stats = sdexec_channel_get_stats (proc->err);
    o = json_pack ("{s:s s:i s:O s:O s:O}",
                   "state", statebuf,
                   "pid", sdexec_unit_pid (proc->unit),
                   "in", in_stats ? in_stats : json_null (),
                   "out", out_stats ? out_stats : json_null (),
                   "err", err_stats ? err_stats : json_null ());
    json_decref (in_stats);
    json_decref (out_stats);
    json_decref (err_stats);
    return o;
}

static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct sdexec_ctx *ctx = arg;
    json_t *procs;
    const flux_msg_t *m;

    if (!(procs = json_object ()))
        goto nomem;
    m = flux_msglist_first (ctx->requests);
    while (m) {
        struct sdproc *proc;
        json_t *entry = NULL;

        if (!(proc = flux_msg_aux_get (m, "sdproc"))
            || !(entry = get_proc_stats (proc))
            || json_object_set_new (procs,
                                    sdexec_unit_name (proc->unit),
                                    entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        m = flux_msglist_next (ctx->requests);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "procs", procs) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (procs);
    return;
nomem:
    errno = ENOMEM;
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (procs);
}

/* When a client (like flux-exec or job-exec) disconnects, send any running
 * units that were started by that UUID a SIGKILL to begin cleanup.  Leave
 * the request in ctx->requests so the unit can be "reaped".  Let normal
 * cleanup of the request (including generating a response which shouldn't
 * hurt) to occur when that happens.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct sdexec_ctx *ctx = arg;
    const flux_msg_t *request;

    request = flux_msglist_first (ctx->requests);
    while (request) {
        if (flux_disconnect_match (msg, request)) {
            struct sdproc *proc = flux_msg_aux_get (request, "sdproc");
            if (proc) {
                flux_future_t *f;
                f = sdexec_kill_unit (h,
                                      ctx->rank,
                                      sdexec_unit_name (proc->unit),
                                      "main",
                                       SIGKILL);
                flux_future_destroy (f);
            }
        }
        request = flux_msglist_next (ctx->requests);
    }
}

/* N.B. systemd.enable is checked in rc1 and ignored here since
 * it should be OK to load the module manually for testing.
 */
static int sdexec_configure (struct sdexec_ctx *ctx,
                             const flux_conf_t *conf,
                             flux_error_t *error)
{
    flux_error_t conf_error;
    int debug = 0;

    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?{s?b}}",
                          "systemd",
                            "sdexec-debug", &debug) < 0) {
        errprintf (error,
                   "error reading [systemd] config table: %s",
                   conf_error.text);
        return -1;
    }
    sdexec_debug = (debug ? true : false);
    return 0;
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct sdexec_ctx *ctx = arg;
    const flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0) {
        errstr = "Failed to parse config-reload request";
        goto error;
    }
    if (sdexec_configure (ctx, conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "disconnect",
      disconnect_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "exec",
      exec_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "write",
      write_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "kill",
      kill_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "list",
      list_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "stats-get",
      stats_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "config-reload",
      config_reload_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END
};

static void sdexec_ctx_destroy (struct sdexec_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->requests) {
            const flux_msg_t *msg;
            msg = flux_msglist_first (ctx->requests);
            while (msg) {
                const char *errstr = "sdexec module is unloading";
                if (flux_respond_error (ctx->h, msg, ENOSYS, errstr) < 0)
                    flux_log_error (ctx->h, "error responding to exec request");
                msg = flux_msglist_next (ctx->requests);
            }
            flux_msglist_destroy (ctx->requests);
        }
        flux_msglist_destroy (ctx->kills);
        free (ctx->local_uri);
        free (ctx);
        errno = saved_errno;
    }
}

static struct sdexec_ctx *sdexec_ctx_create (flux_t *h)
{
    struct sdexec_ctx *ctx;
    const char *s;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    if (flux_get_rank (h, &ctx->rank) < 0)
        goto error;
    if (!(s = flux_attr_get (h, "local-uri"))
        || !(ctx->local_uri = strdup (s)))
        goto error;
    if (!(ctx->requests = flux_msglist_create ())
        || !(ctx->kills = flux_msglist_create ()))
        goto error;
    return ctx;
error:
    sdexec_ctx_destroy (ctx);
    return NULL;
}

/* Check if the sdbus module is loaded on the local rank by pinging its
 * stats-get method.  N.B. sdbus handles its D-bus connect asynchronously
 * so stats-get should be responsive even if D-Bus is not.
 */
static int sdbus_is_loaded (flux_t *h, uint32_t rank, flux_error_t *error)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h, "sdbus.stats-get", NULL, rank, 0))
        || flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOSYS)
            errprintf (error, "sdbus module is not loaded");
        else
            errprintf (error, "sdbus: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static int check_security_version (void)
{
#if HAVE_FLUX_SECURITY
    int major, minor;

    if (flux_security_version (&major, &minor, NULL) < 0
        || (major == 0 && minor < 14)) {
        errno = EINVAL;
        return -1;
    }
#endif /* HAVE_FLUX_SECURITY */
    return 0;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct sdexec_ctx *ctx;
    flux_error_t error;
    int rc = -1;

    /* sdexec launches work in transient Type=notify containers, but
     * this functionality requires flux-security v0.14.0 or greater.
     * Check for correct flux-security version here and abort if the
     * requirement is not met (check is skipped if flux-core is not
     * built with flux-security):
     */
    if (check_security_version () < 0) {
        flux_log (h, LOG_ERR, "sdexec requires flux-security >= v0.14.0 ");
        return -1;
    }

    if (!(ctx = sdexec_ctx_create (h)))
        goto error;
    if (sdexec_configure (ctx, flux_get_conf (h), &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (flux_msg_handler_addvec_ex (h,
                                    MODULE_NAME,
                                    htab,
                                    ctx,
                                    &ctx->handlers) < 0)
        goto error;
    if (sdbus_is_loaded (h, ctx->rank, &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "reactor exited abnormally");
        goto error;
    }
    rc = 0;
error:
    sdexec_ctx_destroy (ctx);
    return rc;
}

MOD_NAME (MODULE_NAME);

// vi:ts=4 sw=4 expandtab
