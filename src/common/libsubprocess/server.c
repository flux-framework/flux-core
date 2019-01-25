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
#include <sodium.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/macros.h"

#include "subprocess.h"
#include "subprocess_private.h"
#include "command.h"
#include "remote.h"
#include "server.h"
#include "util.h"

static int store_pid (flux_subprocess_server_t *s, flux_subprocess_t *p)
{
    pid_t pid = flux_subprocess_pid (p);
    char *str = NULL;
    int rv = -1;

    if (asprintf (&str, "%d", pid) < 0) {
        flux_log_error (s->h, "%s: asprintf", __FUNCTION__);
        goto cleanup;
    }

    if (zhash_insert (s->subprocesses, str, p) < 0) {
        flux_log_error (s->h, "%s: zhash_insert", __FUNCTION__);
        goto cleanup;
    }

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
    flux_subprocess_server_t *s = flux_subprocess_aux_get (p, "server_ctx");
    flux_msg_t *msg = (flux_msg_t *) flux_subprocess_aux_get (p, "msg");

    assert (s && msg);

    remove_pid (s, p);
    flux_subprocess_unref (p);
}

static void rexec_completion_cb (flux_subprocess_t *p)
{
    flux_subprocess_server_t *s = flux_subprocess_aux_get (p, "server_ctx");
    flux_msg_t *msg = (flux_msg_t *) flux_subprocess_aux_get (p, "msg");

    assert (s && msg);

    if (p->state != FLUX_SUBPROCESS_FAILED) {
        /* no fallback if this fails */
        if (flux_respond_pack (s->h, msg, "{s:s s:i}",
                               "type", "complete",
                               "rank", s->rank) < 0)
            flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
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
    flux_subprocess_server_t *s = flux_subprocess_aux_get (p, "server_ctx");
    flux_msg_t *msg = (flux_msg_t *) flux_subprocess_aux_get (p, "msg");

    assert (s && msg);

    if (state == FLUX_SUBPROCESS_STARTED) {
        if (flux_respond_pack (s->h, msg, "{s:s s:i s:i}",
                               "type", "state",
                               "rank", s->rank,
                               "state", state) < 0) {
            flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
    } else if (state == FLUX_SUBPROCESS_RUNNING) {
        if (store_pid (s, p) < 0)
            goto error;

        if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", s->rank,
                               "pid", flux_subprocess_pid (p),
                               "state", state) < 0) {
            flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
    } else if (state == FLUX_SUBPROCESS_EXITED) {
        if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", s->rank,
                               "state", state,
                               "status", flux_subprocess_status (p)) < 0) {
            flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
    } else if (state == FLUX_SUBPROCESS_FAILED) {
        if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:i}",
                               "type", "state",
                               "rank", s->rank,
                               "state", FLUX_SUBPROCESS_FAILED,
                               "errno", p->failed_errno) < 0) {
            flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
        subprocess_cleanup (p);
    } else {
        errno = EPROTO;
        flux_log_error (s->h, "%s: illegal state", __FUNCTION__);
        goto error;
    }

    return;

error:
    internal_fatal (s, p);
}

static int rexec_output_data (flux_subprocess_t *p, const char *stream,
                              flux_subprocess_server_t *s, flux_msg_t *msg,
                              const char *data, int len)
{
    char *s_data = NULL;
    int s_len;
    int rv = -1;

    assert (len);

    s_len = sodium_base64_encoded_len (len, sodium_base64_VARIANT_ORIGINAL);

    if (!(s_data = calloc (1, s_len))) {
        flux_log_error (s->h, "%s: calloc", __FUNCTION__);
        goto error;
    }

    sodium_bin2base64 (s_data, s_len, (unsigned char *)data, len,
                       sodium_base64_VARIANT_ORIGINAL);

    if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:s s:s}",
                           "type", "output",
                           "rank", s->rank,
                           "pid", flux_subprocess_pid (p),
                           "stream", stream,
                           "data", s_data) < 0) {
        flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    rv = 0;
error:
    free (s_data);
    return rv;
}

static int rexec_output_eof (flux_subprocess_t *p, const char *stream,
                             flux_subprocess_server_t *s, flux_msg_t *msg)
{
    if (flux_respond_pack (s->h, msg, "{s:s s:i s:i s:s s:i}",
                           "type", "output",
                           "rank", s->rank,
                           "pid", flux_subprocess_pid (p),
                           "stream", stream,
                           "eof", 1) < 0) {
        flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void rexec_output_cb (flux_subprocess_t *p, const char *stream)
{
    flux_subprocess_server_t *s = flux_subprocess_aux_get (p, "server_ctx");
    flux_msg_t *msg = (flux_msg_t *) flux_subprocess_aux_get (p, "msg");
    const char *ptr;
    int lenp;

    assert (s && msg);

    if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
        flux_log_error (s->h, "%s: flux_subprocess_read", __FUNCTION__);
        goto error;
    }

    if (lenp) {
        if (rexec_output_data (p, stream, s, msg, ptr, lenp) < 0)
            goto error;
    }
    else {
        if (rexec_output_eof (p, stream, s, msg) < 0)
            goto error;
    }

    return;

error:
    internal_fatal (s, p);
}

static void flux_msg_destroy_wrapper (void *arg)
{
    flux_msg_t *msg = arg;
    flux_msg_destroy (msg);
}

static void server_exec_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    flux_subprocess_server_t *s = arg;
    const char *cmd_str;
    flux_cmd_t *cmd = NULL;
    flux_msg_t *copy = NULL;
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

    if (!flux_cmd_getcwd (cmd)) {
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

    if (flux_cmd_setenvf (cmd, 1, "FLUX_URI", s->local_uri) < 0)
        goto error;

    if (flux_respond_pack (s->h, msg, "{s:s s:i}",
                           "type", "start",
                           "rank", s->rank) < 0) {
        flux_log_error (s->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    if (!(p = flux_exec (s->h, FLUX_SUBPROCESS_FLAGS_SETPGRP, cmd, &ops))) {
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

    if (!(copy = flux_msg_copy (msg, true)))
        goto error;
    if (flux_subprocess_aux_set (p, "msg", copy, flux_msg_destroy_wrapper) < 0)
        goto error;
    copy = NULL;                /* owned by 'p' now */
    if (flux_subprocess_aux_set (p, "server_ctx", s, NULL) < 0)
        goto error;

    flux_cmd_destroy (cmd);
    free (env);
    return;

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
cleanup:
    flux_cmd_destroy (cmd);
    free (env);
    flux_msg_destroy (copy);
    flux_subprocess_unref (p);
}

static int write_subprocess (flux_subprocess_server_t *s, flux_subprocess_t *p,
                             const char *name, const char *s_data)
{
    int save_errno;
    size_t s_len, len;
    char *data = NULL;
    int tmp, rv = -1;

    s_len = strlen (s_data);
    len = BASE64_DECODE_SIZE (s_len);

    if (!(data = calloc (1, len))) {
        flux_log_error (s->h, "%s: calloc", __FUNCTION__);
        goto cleanup;
    }

    if (sodium_base642bin ((unsigned char *)data, len, s_data, s_len,
                           NULL, &len, NULL,
                           sodium_base64_VARIANT_ORIGINAL) < 0) {
        flux_log_error (s->h, "%s: sodium_base642bin", __FUNCTION__);
        goto cleanup;
    }

    if ((tmp = flux_subprocess_write (p, name, data, len)) < 0) {
        flux_log_error (s->h, "%s: flux_subprocess_write", __FUNCTION__);
        goto cleanup;
    }

    /* add list of msgs if there is overflow? */

    if (tmp != len) {
        flux_log_error (s->h, "channel buffer error: rank = %d pid = %d, stream = %s, len = %zu",
                        s->rank, flux_subprocess_pid (p), name, len);
        errno = EOVERFLOW;
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    free (data);
    errno = save_errno;
    return rv;
}

static int close_subprocess (flux_subprocess_server_t *s, flux_subprocess_t *p,
                             const char *name)
{
    if (flux_subprocess_close (p, name) < 0) {
        flux_log_error (s->h, "%s: flux_subprocess_close", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void server_write_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    flux_subprocess_t *p;
    flux_subprocess_server_t *s = arg;
    const char *name;
    pid_t pid;
    int close_flag;

    if (flux_request_unpack (msg, NULL, "{ s:i s:s s:i }",
                             "pid", &pid,
                             "name", &name,
                             "close", &close_flag) < 0) {
        /* can't handle error, no pid to sent errno back to, so just
         * return */
        flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
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
        if (!(errno == ENOENT && close_flag))
            flux_log_error (s->h, "%s: lookup_pid", __FUNCTION__);
        return;
    }

    /* Chance subprocess exited/killed/etc. since user write request
     * was sent.
     */
    if (p->state != FLUX_SUBPROCESS_RUNNING)
        return;

    if (close_flag) {
        if (close_subprocess (s, p, name) < 0)
            goto error;
    }
    else {
        const char *data;

        if (flux_request_unpack (msg, NULL, "{ s:s }",
                                 "data", &data) < 0) {
            flux_log_error (s->h, "%s: flux_request_unpack", __FUNCTION__);
            errno = EPROTO;
            goto error;
        }

        if (write_subprocess (s, p, name, data) < 0)
            goto error;
    }

    return;

error:
    internal_fatal (s, p);
}

static void server_signal_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
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

error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

char *subprocess_sender (flux_subprocess_t *p)
{
    flux_msg_t *msg;
    char *sender;

    msg = flux_subprocess_aux_get (p, "msg");
    if (!msg || flux_msg_get_route_first (msg, &sender) < 0)
        return NULL;

    return sender;
}

static json_t *process_info (flux_subprocess_t *p)
{
    flux_cmd_t *cmd;
    char *cmd_str = NULL;
    char *sender = NULL;
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
    free (sender);
    free (cmd_str);
    return info;
}

static void server_processes_cb (flux_t *h, flux_msg_handler_t *mh,
                                 const flux_msg_t *msg, void *arg)
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
        if (!(o = process_info (p))
            || json_array_append_new (procs, o) < 0) {
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
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (procs);
}

int server_start (flux_subprocess_server_t *s, const char *prefix)
{
    /* rexec.processes is primarily for testing */
    struct flux_msg_handler_spec htab[] = {
        { FLUX_MSGTYPE_REQUEST, "rexec",        server_exec_cb, 0 },
        { FLUX_MSGTYPE_REQUEST, "rexec.write",  server_write_cb, 0 },
        { FLUX_MSGTYPE_REQUEST, "rexec.signal", server_signal_cb, 0 },
        { FLUX_MSGTYPE_REQUEST, "rexec.processes", server_processes_cb, 0 },
        FLUX_MSGHANDLER_TABLE_END,
    };
    char *topic_globs[4] = {NULL, NULL, NULL, NULL};
    int rv = -1;

    assert (prefix);

    if (asprintf (&topic_globs[0], "%s.rexec", prefix) < 0)
        goto cleanup;
    if (asprintf (&topic_globs[1], "%s.rexec.write", prefix) < 0)
        goto cleanup;
    if (asprintf (&topic_globs[2], "%s.rexec.signal", prefix) < 0)
        goto cleanup;
    if (asprintf (&topic_globs[3], "%s.rexec.processes", prefix) < 0)
        goto cleanup;

    htab[0].topic_glob = (const char *)topic_globs[0];
    htab[1].topic_glob = (const char *)topic_globs[1];
    htab[2].topic_glob = (const char *)topic_globs[2];
    htab[3].topic_glob = (const char *)topic_globs[3];

    if (flux_msg_handler_addvec (s->h, htab, s, &s->handlers) < 0)
        goto cleanup;

    rv = 0;
cleanup:
    free (topic_globs[0]);
    free (topic_globs[1]);
    free (topic_globs[2]);
    free (topic_globs[3]);
    return rv;
}

void server_stop (flux_subprocess_server_t *s)
{
    flux_msg_handler_delvec (s->handlers);
}

static void terminate (flux_subprocess_t *p)
{
    flux_future_t *f;
    if (!(f = flux_subprocess_kill (p, SIGKILL))) {
        flux_subprocess_server_t *s;
        s = flux_subprocess_aux_get (p, "server_ctx");
        flux_log_error (s->h, "%s: flux_subprocess_kill", __FUNCTION__);
        return;
    }
    flux_future_destroy (f);
}

int server_terminate_subprocesses (flux_subprocess_server_t *s)
{
    flux_subprocess_t *p;

    p = zhash_first (s->subprocesses);
    while (p) {
        terminate (p);
        p = zhash_next (s->subprocesses);
    }

    return 0;
}

static void terminate_uuid (flux_subprocess_t *p, const char *id)
{
    char *sender;

    if (!(sender = subprocess_sender (p)))
        return;

    if (!strcmp (id, sender))
        terminate (p);

    free (sender);
}

int server_terminate_by_uuid (flux_subprocess_server_t *s,
                              const char *id)
{
    flux_subprocess_t *p;

    p = zhash_first (s->subprocesses);
    while (p) {
        terminate_uuid (p, id);
        p = zhash_next (s->subprocesses);
    }

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
