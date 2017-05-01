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
#include <flux/core.h>

#include "src/common/libsubprocess/zio.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "attr.h"
#include "exec.h"

typedef struct {
    flux_t *h;
    struct subprocess_manager *sm;
    uint32_t rank;
    const char *local_uri;
} exec_t;

static json_object *
subprocess_json_resp (exec_t *x, struct subprocess *p)
{
    json_object *resp = Jnew ();

    assert (x != NULL);
    assert (resp != NULL);

    Jadd_int (resp, "rank", x->rank);
    Jadd_int (resp, "pid", subprocess_pid (p));
    Jadd_str (resp, "state", subprocess_state_string (p));
    return (resp);
}

static char *prepare_exit_payload (exec_t *x, struct subprocess *p)
{
    int n;
    json_object *resp = Jnew ();
    char *s;

    Jadd_int (resp, "rank", x->rank);
    Jadd_int (resp, "pid", subprocess_pid (p));
    Jadd_str (resp, "state", subprocess_state_string (p));
    Jadd_int (resp, "status", subprocess_exit_status (p));
    Jadd_int (resp, "code", subprocess_exit_code (p));
    if ((n = subprocess_signaled (p)))
        Jadd_int (resp, "signal", n);
    if ((n = subprocess_exec_error (p)))
        Jadd_int (resp, "exec_errno", n);
    if (!(s = strdup (Jtostr (resp)))) {
        errno = ENOMEM;
        goto error;
    }
    json_object_put (resp);
    return s;
error:
    json_object_put (resp);
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
    json_object *o = NULL;
    char *s;

    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto error;
    }
    /* Add this rank */
    Jadd_int (o, "rank", x->rank);
    if (!(s = strdup (Jtostr (o)))) {
        errno = ENOMEM;
        goto error;
    }
    return s;
error:
    json_object_put (o);
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

static void write_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    json_object *request = NULL;
    json_object *o = NULL;
    const char *json_str;
    int pid;
    int errnum = 0;
    struct subprocess *p;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        errnum = errno;
        goto out;
    }
    if (!json_str
        || !(request = Jfromstr (json_str))
        || !Jget_int (request, "pid", &pid)
        || !Jget_obj (request, "stdin", &o)) {
        errnum = EPROTO;
        goto out;
    }
    if (!(p = subprocess_get_pid (x->sm, pid))) {
        errnum = ENOENT;
        goto out;
    }
    if (write_to_child (p, Jtostr (o)) < 0) {
        errnum = errno;
        goto out;
    }
out:
    if (flux_respondf (h, msg, "{ s:i }", "code", errnum) < 0)
        flux_log_error (h, "write_request_cb: flux_respondf");
    json_object_put (request);
}

static void signal_request_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    int pid;
    int errnum = EPROTO;
    int signum = SIGTERM;
    struct subprocess *p;

    if (flux_request_decodef (msg, NULL, "{s:i s?:i}",
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
    if (flux_respondf (h, msg, "{ s:i }", "code", errnum) < 0)
        flux_log_error (h, "signal_request_cb: flux_respondf");
}

static int do_setpgrp (struct subprocess *p)
{
    if (setpgrp () < 0)
        fprintf (stderr, "setpgrp: %s", strerror (errno));
    return (0);
}

/*
 *  Create a subprocess described in the msg argument.
 */
static void exec_request_cb (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    exec_t *x = arg;
    json_object *request = NULL;
    json_object *response = NULL;
    json_object *o;
    const char *json_str;
    struct subprocess *p;
    flux_msg_t *copy;
    int i, argc;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto out_free;

    if (!json_str
        || !(request = Jfromstr (json_str))
        || !json_object_object_get_ex (request, "cmdline", &o)
        || o == NULL
        || (json_object_get_type (o) != json_type_array)) {
        errno = EPROTO;
        goto out_free;
    }

    if ((argc = json_object_array_length (o)) < 0) {
        errno = EPROTO;
        goto out_free;
    }

    p = subprocess_create (x->sm);
    subprocess_set_context (p, "exec_ctx", x);
    subprocess_add_hook (p, SUBPROCESS_COMPLETE, child_exit_handler);
    subprocess_add_hook (p, SUBPROCESS_PRE_EXEC, do_setpgrp);

    for (i = 0; i < argc; i++) {
        json_object *ox = json_object_array_get_idx (o, i);
        if (json_object_get_type (ox) == json_type_string)
            subprocess_argv_append (p, json_object_get_string (ox));
    }

    if (json_object_object_get_ex (request, "env", &o) && o != NULL) {
        json_object_iter iter;
        json_object_object_foreachC (o, iter) {
            const char *val = json_object_get_string (iter.val);
            if (val != NULL)
                subprocess_setenv (p, iter.key, val, 1);
        }
    }
    else
        subprocess_set_environ (p, environ);
    /*
     *  Override key FLUX environment variables in env array
     */
    subprocess_setenv (p, "FLUX_URI", x->local_uri, 1);

    if (json_object_object_get_ex (request, "cwd", &o) && o != NULL) {
        const char *dir = json_object_get_string (o);
        if (dir != NULL)
            subprocess_set_cwd (p, dir);
    }

    /*
     * Save a copy of msg for future messages
     */
    copy = flux_msg_copy (msg, true);
    subprocess_set_context (p, "msg", (void *) copy);

    subprocess_set_io_callback (p, child_io_cb);

    if (subprocess_fork (p) < 0) {
        /*
         *  Fork error, respond directly to exec client with error
         *   (There will be no subprocess to reap)
         */
        (void) flux_respond (h, msg, errno, NULL);
        goto out_free;
    }

    if (subprocess_exec (p) >= 0) {
        /*
         *  Send response, destroys original msg.
         *   For "Exec Failure" allow that state to be transmitted
         *   to caller on completion handler (which will be called
         *   immediately)
         */
        response = subprocess_json_resp (x, p);
        flux_respond (h, msg, 0, Jtostr (response));
    }
out_free:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
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

static json_object *subprocess_json_info (struct subprocess *p)
{
    int i;
    char buf [MAXPATHLEN];
    const char *cwd;
    char *sender = NULL;
    json_object *o = Jnew ();
    json_object *a = Jnew_ar ();

    Jadd_int (o, "pid", subprocess_pid (p));
    for (i = 0; i < subprocess_get_argc (p); i++) {
        Jadd_ar_str (a, subprocess_get_arg (p, i));
    }
    /*  Avoid shortjson here so we don't take
     *   unnecessary reference to 'a'
     */
    json_object_object_add (o, "cmdline", a);
    if ((cwd = subprocess_get_cwd (p)) == NULL)
        cwd = getcwd (buf, MAXPATHLEN-1);
    Jadd_str (o, "cwd", cwd);
    if ((sender = subprocess_sender (p))) {
        Jadd_str (o, "sender", sender);
        free (sender);
    }
    return (o);
}

static void ps_request_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    struct subprocess *p;
    exec_t *x = arg;
    json_object *out = Jnew ();
    json_object *procs = Jnew_ar ();

    Jadd_int (out, "rank", x->rank);

    p = subprocess_manager_first (x->sm);
    while (p) {
        json_object *o = subprocess_json_info (p);
        /* Avoid shortjson here so we don't take an unnecessary
         *  reference to 'o'.
         */
        json_object_array_add (procs, o);
        p = subprocess_manager_next (x->sm);
    }
    json_object_object_add (out, "procs", procs);
    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (out);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "cmb.exec",           exec_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.exec.signal",    signal_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.exec.write",     write_request_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.processes",      ps_request_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

static void exec_finalize (void *arg)
{
    exec_t *x = arg;
    free (x);
    flux_msg_handler_delvec (handlers);
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
    if (flux_msg_handler_addvec (h, handlers, x) < 0) {
        free (x);
        return -1;
    }
    flux_aux_set (h, "flux::exec", x, exec_finalize);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
