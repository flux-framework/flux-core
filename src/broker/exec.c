/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libsubprocess/zio.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libutil/log.h"

#include "attr.h"
#include "exec.h"

typedef struct {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct subprocess_manager *sm;
    uint32_t rank;
    const char *local_uri;
} exec_t;

static char *prepare_exit_payload (exec_t *x, struct subprocess *p)
{
    int n;
    json_t *resp;
    char *s;

    if (!(resp = json_pack ("{s:i s:i s:s s:i s:i}",
                            "rank", x->rank,
                            "pid", subprocess_pid (p),
                            "state", subprocess_state_string (p),
                            "status", subprocess_exit_status (p),
                            "code", subprocess_exit_code (p)))) {
        errno = ENOMEM;
        goto error;
    }
    if ((n = subprocess_signaled (p))) {
        json_t *o = json_integer (n);
        if (!o || json_object_set_new (resp, "signal", o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }

    }
    if ((n = subprocess_exec_error (p))) {
        json_t *o = json_integer (n);
        if (!o || json_object_set_new (resp, "exec_errno", o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }
    }
    if (!(s = json_dumps (resp, 0))) {
        errno = ENOMEM;
        goto error;
    }
    json_decref (resp);
    return s;
error:
    json_decref (resp);
    return NULL;
}

/* Handler for child exit (registered with libsubprocess).
 * Respond to user with exit status, etc.
 * using orig. request message stashed in subprocess context.
 */
static int child_exit_handler (struct subprocess *p)
{
    exec_t *x = subprocess_get_context (p, "exec_ctx");
    flux_msg_t *msg = (flux_msg_t *) subprocess_get_context (p, "msg");
    char *s = NULL;

    assert (x != NULL);
    assert (msg != NULL);

    if (!(s = prepare_exit_payload (x, p))) {
        if (flux_respond (x->h, msg, errno, NULL) < 0)
            flux_log_error (x->h, "%s: flux_respond", __FUNCTION__);
        goto done;
    }
    if (flux_respond (x->h, msg, 0, s) < 0)
        flux_log_error (x->h, "%s: flux_respond", __FUNCTION__);
done:
    free (s);
    flux_msg_destroy (msg);
    subprocess_destroy (p);
    return (0);
}

static char *prepare_io_payload (exec_t *x, const char *json_str)
{
    json_t *resp;
    json_t *o;
    char *s;

    if (!(resp = json_loads (json_str, 0, NULL))) {
        errno = EPROTO;
        goto error;
    }
    if (!(o = json_integer (x->rank))
            || json_object_set_new (resp, "rank", o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        goto error;
    }
    if (!(s = json_dumps (resp, 0))) {
        errno = ENOMEM;
        goto error;
    }
    json_decref (resp);
    return s;
error:
    json_decref (resp);
    return NULL;
}

/* Handler for child stdio (registered with libsubprocess).
 * Respond to user with zio-formatted data, tacking on the rank.
 * using orig. request message stashed in subprocess context.
 */
static int child_io_cb (struct subprocess *p, const char *json_str)
{
    exec_t *x = subprocess_get_context (p, "exec_ctx");
    flux_msg_t *msg = subprocess_get_context (p, "msg");
    char *s;

    assert (x != NULL);
    assert (msg != NULL);

    if (!(s = prepare_io_payload (x, json_str))) {
        if (flux_respond (x->h, msg, errno, NULL) < 0)
            flux_log_error (x->h, "%s: flux_respond", __FUNCTION__);
        goto done;
    }
    if (flux_respond (x->h, msg, 0, s) < 0)
        flux_log_error (x->h, "%s: flux_respond", __FUNCTION__);
done:
    free (s);
    return (0); // return value is not checked in libsubprocess
}

static struct subprocess *
subprocess_get_pid (struct subprocess_manager *sm, int pid)
{
    struct subprocess *p = NULL;
    p = subprocess_manager_first (sm);
    while (p) {
        if (pid == subprocess_pid (p))
            return (p);
        p = subprocess_manager_next (sm);
    }
    return (NULL);
}

static int write_to_child (struct subprocess *p, const char *s)
{
    int len;
    void *data = NULL;
    bool eof;
    int rc = -1;

    /* XXX: We use zio_json_decode() here for convenience. Probably
     *  this should be bubbled up as a subprocess IO json spec with
     *  encode/decode functions.
     */
     if ((len = zio_json_decode (s, &data, &eof)) < 0)
        goto done;
    if (subprocess_write (p, data, len, eof) < 0)
        goto done;
    rc = 0;
done:
    free (data);
    return rc;
}

static void write_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    json_t *o;
    char *s = NULL;
    int pid;
    int errnum = 0;
    struct subprocess *p;

    if (flux_request_unpack (msg, NULL, "{s:i s:o}", "pid", &pid,
                                                     "stdin", &o) < 0) {
        errnum = errno;
        goto out;
    }
    if (!(p = subprocess_get_pid (x->sm, pid))) {
        errnum = ENOENT;
        goto out;
    }
    if (!(s = json_dumps (o, 0))) {
        errnum = EPROTO;
        goto out;
    }
    if (write_to_child (p, s) < 0) {
        errnum = errno;
        goto out;
    }
out:
    free (s);
    if (flux_respond_pack (h, msg, "{ s:i }", "code", errnum) < 0)
        flux_log_error (h, "write_request_cb: flux_respond_pack");
}

static void signal_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    int pid;
    int errnum = EPROTO;
    int signum = SIGTERM;
    struct subprocess *p;

    if (flux_request_unpack (msg, NULL, "{s:i s?:i}",
                             "pid", &pid,
                             "signum", &signum) < 0) {
        errnum = errno;
        goto out;
    }
    p = subprocess_manager_first (x->sm);
    while (p) {
        if (pid == subprocess_pid (p)) {
            errnum = 0;
            /* Send signal to entire process group */
            if (kill (-pid, signum) < 0)
                errnum = errno;
        }
        p = subprocess_manager_next (x->sm);
    }
out:
    if (flux_respond_pack (h, msg, "{ s:i }", "code", errnum) < 0)
        flux_log_error (h, "signal_request_cb: flux_respond_pack");
}

static int do_setpgrp (struct subprocess *p)
{
    if (setpgrp () < 0)
        fprintf (stderr, "setpgrp: %s", strerror (errno));
    return (0);
}


static int prepare_subprocess (exec_t *x,
                               json_t *args,
                               json_t *env,
                               const char *cwd,
                               const flux_msg_t *msg,
                               struct subprocess **pp)
{
    struct subprocess *p;
    const char *s;
    flux_msg_t *copy = NULL;
    const char *key;
    size_t index;
    json_t *o;

    if (!(p = subprocess_create (x->sm)))
        goto error;
    if (subprocess_add_hook (p, SUBPROCESS_COMPLETE, child_exit_handler) < 0)
        goto error;
    if (subprocess_add_hook (p, SUBPROCESS_PRE_EXEC, do_setpgrp) < 0)
        goto error;
    if (subprocess_set_io_callback (p, child_io_cb) < 0)
        goto error;
    /* Save context for subprocess callbacks.
     * Include request message for multiple responses.
     */
    if (!(copy = flux_msg_copy (msg, true)))
        goto error;
    if (subprocess_set_context (p, "msg", (void *) copy) < 0)
        goto error;
    subprocess_set_context (p, "exec_ctx", x);
    /* Command and arguments
     */
    json_array_foreach (args, index, o) {
        if (!(s = json_string_value (o))) {
            errno = EPROTO;
            goto error;
        }
        if (subprocess_argv_append (p, s) < 0)
            goto error;
    }
    /* Environment
     */
    if (env) {
        json_object_foreach (env, key, o) {
            if (!(s = json_string_value (o))) {
                errno = EPROTO;
                goto error;
            }
            if (subprocess_setenv (p, key, s, 1) < 0)
                goto error;
        }
    }
    else {
        if (subprocess_set_environ (p, environ) < 0)
            goto error;
    }
    if (subprocess_setenv (p, "FLUX_URI", x->local_uri, 1) < 0)
        goto error;
    /* Working directory
     */
    if (cwd) {
        if (subprocess_set_cwd (p, cwd) < 0)
            goto error;
    }

    *pp = p;
    return 0;
error:
    flux_msg_destroy (copy);
    subprocess_destroy (p);
    return -1;
}

static void exec_request_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    json_t *args;
    json_t *env = NULL;
    const char *cwd = NULL;
    struct subprocess *p;

    if (flux_request_unpack (msg, NULL, "{s:o s?:o s?:s}",
                             "cmdline", &args,
                             "env", &env,
                             "cwd", &cwd) < 0)
        goto error;
    if (prepare_subprocess (x, args, env, cwd, msg, &p) < 0)
        goto error;

    if (subprocess_fork (p) < 0) {
        /*
         *  Fork error, respond directly to exec client with error
         *   (There will be no subprocess to reap)
         */
        goto error;
    }

    if (subprocess_exec (p) >= 0) {
        /*
         *  Successful exec response.
         *   For "Exec Failure" allow that state to be transmitted
         *   to caller on completion handler (which will be called
         *   immediately)
         */
        if (flux_respond_pack (h, msg, "{s:i s:i s:s}",
                               "rank", x->rank,
                               "pid", subprocess_pid (p),
                               "state", subprocess_state_string (p)) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static char *subprocess_sender (struct subprocess *p)
{
    char *sender;
    flux_msg_t *msg = subprocess_get_context (p, "msg");
    if (!msg || flux_msg_get_route_first (msg, &sender) < 0)
        return NULL;
    return (sender);
}

int exec_terminate_subprocesses_by_uuid (flux_t *h, const char *id)
{
    exec_t *x = flux_aux_get (h, "flux::exec");

    struct subprocess *p = subprocess_manager_first (x->sm);
    while (p) {
        char *sender;
        if ((sender = subprocess_sender (p))) {
            pid_t pid;
            if ((strcmp (id, sender) == 0)
               && ((pid = subprocess_pid (p)) > (pid_t) 0)) {
                /* Kill process group for subprocess p */
                flux_log (x->h, LOG_INFO,
                          "Terminating PGRP %ld", (unsigned long) pid);
                if (kill (-pid, SIGKILL) < 0)
                    flux_log_error (x->h, "killpg");
            }
            free (sender);
        }
        p = subprocess_manager_next (x->sm);
    }
    return (0);
}

static json_t *subprocess_json_info (struct subprocess *p)
{
    int i;
    char buf [MAXPATHLEN];
    const char *cwd;
    char *sender = NULL;
    json_t *info = NULL;
    json_t *args = NULL;

    if ((cwd = subprocess_get_cwd (p)) == NULL) {
        if (!(cwd = getcwd (buf, MAXPATHLEN-1)))
            goto error;
    }
    if (!(args = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    for (i = 0; i < subprocess_get_argc (p); i++) {
        json_t *o;
        if (!(o = json_string (subprocess_get_arg (p, i)))
               || json_array_append_new (args, o) < 0) {
            json_decref (o);
            json_decref (args);
            errno = ENOMEM;
            goto error;
        }
    }
    if (!(info = json_pack ("{s:i s:s s:o}",
                            "pid", subprocess_pid (p),
                            "cwd", cwd,
                            "cmdline", args))) {
        json_decref (args);
        errno = ENOMEM;
        goto error;
    }
    if ((sender = subprocess_sender (p))) {
        json_t *o;
        if (!(o = json_string (sender))
               || json_object_set_new (info, "sender", o) < 0) {
            json_decref (o);
            free (sender);
            errno = ENOMEM;
            goto error;
        }
        free (sender);
    }
    return (info);
error:
    json_decref (info);
    return NULL;
}

static void ps_request_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct subprocess *p;
    exec_t *x = arg;
    json_t *procs;

    if (!(procs = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    p = subprocess_manager_first (x->sm);
    while (p) {
        json_t *o;
        if (!(o = subprocess_json_info (p))
                || json_array_append_new (procs, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }
        p = subprocess_manager_next (x->sm);
    }
    if (flux_respond_pack (h, msg, "{s:i s:o}", "rank", x->rank,
                                                "procs", procs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (procs);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "cmb.exec",           exec_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.exec.signal",    signal_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.exec.write",     write_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.processes",      ps_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void exec_finalize (void *arg)
{
    exec_t *x = arg;
    flux_msg_handler_delvec (x->handlers);
    free (x);
}

int exec_initialize (flux_t *h, struct subprocess_manager *sm,
                     uint32_t rank, attr_t *attrs)
{
    exec_t *x = calloc (1, sizeof (*x));
    if (!x) {
        errno = ENOMEM;
        return -1;
    }
    x->h = h;
    x->sm = sm;
    x->rank = rank;
    if (attr_get (attrs, "local-uri", &x->local_uri, NULL) < 0) {
        free (x);
        return -1;
    }
    if (flux_msg_handler_addvec (h, htab, x, &x->handlers) < 0) {
        free (x);
        return -1;
    }
    flux_aux_set (h, "flux::exec", x, exec_finalize);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
