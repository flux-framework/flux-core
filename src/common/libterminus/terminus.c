/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* libterminus - Termin(al) User Services for Flux
 *
 * Manages multiple Flux pty sessions behind a common *-terminus
 *  service endpoint. Supports;
 *
 * - Create new terminal session: *terminus.new
 *
 *   IN:   { "name":s "cmd":[] "environ":{} "cwd":s }
 *   OUT:  { "name":s "pty_service":s "id":i }
 *
 * - List current terminal sessions: *terminus.list
 *
 *   IN:   {}
 *   OUT:  { "server":{ "service":s "rank":i "ctime":f }
 *           "sessions":[ { "id":i "name":s "clients"i
 *                          "pid"i "ctime":f }, ...
 *                      ]
 *         }
 *
 * - Kill terminal sessions by ID: *terminus.kill
 *   If 'wait', then response will be delayed until session exits
 *
 *   IN:   { "id":i "signal":i "wait"?i }
 *   OUT:  {}
 *
 * - Kill all sessions: *terminus.kill-server
 *
 *   IN:   {}
 *   OUT:  {} (response after all sessions exit)
 *
 *
 * Sessions are managed on *terminus.ID service endpoints.
 * Once the session ID is known, a client may connect directly to
 * to the pty server at this service, using the protocol detailed
 * in pty.c.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include <jansson.h>

#include <flux/idset.h>

#define LLOG_SUBSYSTEM "pty"

#include "src/common/libutil/llog.h"

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libterminus/pty.h"

#include "terminus.h"

extern char **environ;

struct terminus_session {
    struct flux_terminus_server *server;
    int refcount;

    char *name;
    char topic[128];
    flux_msg_handler_t *mh;

    int id;
    double ctime;

    flux_subprocess_t *p;
    flux_cmd_t *cmd;

    bool wait_on_attach; /* wait at least one attach before reaping */
    bool exited;         /* true if subprocess exited */

    struct flux_pty *pty;
};

struct empty_waiter {
    flux_terminus_server_empty_f cb;
    void *arg;
};

struct flux_terminus_server {
    flux_t *h;
    uint32_t rank;

    flux_msg_handler_t **handlers;

    terminus_log_f llog;
    void *llog_data;

    char service[128];

    struct idset *idset;

    double ctime;
    zlist_t *sessions;
    zlist_t *empty_waiters;
};

static void terminus_session_incref (struct terminus_session *s)
{
    if (s)
        s->refcount++;
}

static void terminus_session_decref (struct terminus_session *s)
{
    if (s && --s->refcount == 0) {
        if (s->id >= 0)
            idset_clear (s->server->idset, s->id);
        flux_pty_destroy (s->pty);
        flux_msg_handler_destroy (s->mh);
        free (s->name);
        flux_subprocess_destroy (s->p);
        free (s);
    }
}

static void terminus_session_destroy (struct terminus_session *s)
{
    terminus_session_decref (s);
}

static void notify_empty_waiters (struct flux_terminus_server *ts)
{
    struct empty_waiter *w;
    while ((w = zlist_pop (ts->empty_waiters))) {
        (*w->cb) (ts, w->arg);
        free (w);
    }
}

static void server_remove_session (struct flux_terminus_server *ts,
                                   struct terminus_session *s)
{
    if (s) {
        zlist_remove (ts->sessions, s);
        if (zlist_size (ts->sessions) == 0)
            notify_empty_waiters (ts);
    }
}

static void session_msg_handler (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    struct terminus_session *s = arg;
    terminus_session_incref (s);
    if (flux_pty_sendmsg (s->pty, msg) < 0)
        llog_error (s->server, "flux_pty_sendmsg: %s", strerror (errno));

    /*  If session is waiting for first attach, and there is 1 or
     *   more clients attached now, then wait can be disabled
     */
    if (s->wait_on_attach && flux_pty_client_count (s->pty) > 0)
        s->wait_on_attach = false;
    terminus_session_decref (s);
}

static int terminus_msg_handler_start (struct terminus_session *s)
{
    struct flux_match match = FLUX_MATCH_REQUEST;

    match.topic_glob = s->topic;
    if (!(s->mh = flux_msg_handler_create (s->server->h,
                                           match,
                                           session_msg_handler,
                                           s)))
        return -1;
    flux_msg_handler_allow_rolemask (s->mh, FLUX_ROLE_USER);
    flux_msg_handler_start (s->mh);
    return 0;
}

static void session_complete (struct flux_pty *pty)
{
    struct terminus_session *s = flux_pty_aux_get (pty, "terminus_session");
    server_remove_session (s->server, s);
}

static struct terminus_session *
terminus_session_create (struct flux_terminus_server *ts,
                         int id,
                         const char *name,
                         bool wait)
{
    struct terminus_session *s = calloc (1, sizeof (*s));
    if (!s)
        return NULL;

    s->refcount = 1;
    s->id = id;
    s->server = ts;
    s->wait_on_attach = wait;
    s->ctime = flux_reactor_now (flux_get_reactor (ts->h));
    if (name && !(s->name = strdup (name)))
        goto error;
    if (!(s->pty = flux_pty_open ()))
        goto error;
    if (flux_pty_set_flux (s->pty, ts->h) < 0
        || flux_pty_aux_set (s->pty, "terminus_session", s, NULL) < 0)
        goto error;
    flux_pty_wait_on_close (s->pty);
    flux_pty_set_complete_cb (s->pty, session_complete);
    if (s->wait_on_attach)
        flux_pty_wait_for_client (s->pty);
    if (ts->llog)
        flux_pty_set_log (s->pty, ts->llog, ts->llog_data);
    if (snprintf (s->topic,
                  sizeof (s->topic),
                  "%s.%d",
                  s->server->service,
                  s->id) >= sizeof (s->topic)) {
        errno = EOVERFLOW;
        goto error;
    }
    if (terminus_msg_handler_start (s) < 0)
        goto error;
    if (zlist_append (ts->sessions, s) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!zlist_freefn (ts->sessions,
                       s,
                       (zlist_free_fn *) terminus_session_destroy,
                       true)) {
        errno = ENOENT;
        goto error;
    }
    return s;
error:
    /*  On error we _possibly_ need remove session from zlist,
     *   but definitely need to manually destroy session, since
     *   zlist_freefn() is guaranteed not to have been called if
     *   we got here.
     */
    if (s) {
        int saved_errno = errno;
        zlist_remove (ts->sessions, s);
        terminus_session_destroy (s);
        errno = saved_errno;
    }
    return NULL;
}

static int terminus_session_kill (struct terminus_session *s, int signum)
{
    /*  When killing a session, clear the wait flag so we don't hang
     *   waiting on the first attach.
     */
    s->wait_on_attach = false;

    /*  Session may have already exited if wait_on_attach.
     *  Close the pty now to avoid a hang.
     */
    if (s->exited) {
        server_remove_session (s->server, s);
        return 0;
    }
    /*  First kill processes using pty, then signal process group,
     *   though they may be one in the same
     */
    if (flux_pty_kill (s->pty, signum) < 0
        || kill (-flux_subprocess_pid (s->p), signum) < 0)
        return -1;

    return 0;
}

#if CODE_COVERAGE_ENABLED
extern void __gcov_dump ();
extern void __gcov_reset ();
#endif
static void terminus_pty_attach (flux_subprocess_t *p, void *arg)
{
    struct terminus_session *s = arg;
    if (flux_pty_attach (s->pty) < 0) {
        llog_fatal (s->server, "terminus: pty attach: %s\n", strerror (errno));
#if CODE_COVERAGE_ENABLED
        __gcov_dump ();
        __gcov_reset ();
#endif
        _exit (1);
    }
}

int flux_terminus_server_session_close (struct flux_terminus_server *ts,
                                        struct flux_pty *pty,
                                        int status)
{
    struct terminus_session *s;

    if (!ts || !pty || status < 0) {
        errno = EINVAL;
        return -1;
    }

    s = zlist_first (ts->sessions);
    while (s) {
        if (s->pty == pty)
            break;
        s = zlist_next (ts->sessions);
    }
    if (s) {
        s->exited = true;
        flux_pty_exited (s->pty, status);
        return 0;
    }
    errno = ENOENT;
    return -1;
}

static void terminus_session_exit (flux_subprocess_t *p)
{
    struct terminus_session *s = flux_subprocess_aux_get (p, "terminus");
    llog_debug (s->server, "session %d exit: pid=%ld status=%d",
                s->id,
                (long) flux_subprocess_pid (p),
                flux_subprocess_status (p));
    s->exited = true;
    flux_pty_exited (s->pty, flux_subprocess_status (p));
}

static int terminus_session_start (struct terminus_session *s,
                                   flux_cmd_t *cmd)
{
    int flags = FLUX_SUBPROCESS_FLAGS_STDIO_FALLTHROUGH;
    flux_subprocess_hooks_t hooks = {
        .pre_exec = terminus_pty_attach,
        .pre_exec_arg = s,
    };
    flux_subprocess_ops_t ops = {
        .on_completion = terminus_session_exit,
    };
    s->cmd = cmd;
    s->p = flux_local_exec_ex (flux_get_reactor (s->server->h),
                               flags,
                               cmd,
                               &ops,
                               &hooks,
                               NULL,
                               NULL);
    if (!s->p)
        goto cleanup;
    if (flux_subprocess_aux_set (s->p, "terminus", s, NULL) < 0)
        goto cleanup;
    return 0;
cleanup:
    flux_subprocess_destroy (s->p);
    s->p = NULL;
    return -1;
}

static void flux_terminus_server_stop (struct flux_terminus_server *ts)
{
    flux_msg_handler_delvec (ts->handlers);
    ts->handlers = NULL;
}

int flux_terminus_server_notify_empty (struct flux_terminus_server *ts,
                                       flux_terminus_server_empty_f cb,
                                       void *arg)
{
    struct empty_waiter *w;

    if (!ts || !cb) {
        errno = EINVAL;
        return -1;
    }
    w = calloc (1, sizeof (*w));
    if (!w)
        return -1;
    w->cb = cb;
    w->arg = arg;
    if (zlist_append (ts->empty_waiters, w) < 0) {
        free (w);
        return -1;
    }
    zlist_freefn (ts->empty_waiters, w, (zlist_free_fn *) free, true);
    return 0;
}

void flux_terminus_server_destroy (struct flux_terminus_server *t)
{
    if (t) {
        flux_terminus_server_stop (t);
        zlist_destroy (&t->empty_waiters);
        zlist_destroy (&t->sessions);
        idset_destroy (t->idset);
        free (t);
    }
}

struct terminus_session * session_lookup (struct flux_terminus_server *ts,
                                          int id)
{
    struct terminus_session *s = zlist_first (ts->sessions);
    while (s) {
        if (s->id == id)
            return s;
        s = zlist_next (ts->sessions);
    }
    return NULL;
}

/*  Build a flux_cmd_t from the msg 'msg'.
 *  All of cmd, environ, and cwd are optional, with defaults '$SHELL'
 *   current environment, and current working directory respectively.
 */
static flux_cmd_t *make_cmd (const flux_msg_t *msg)
{
    int i;
    char cwd_buf [4096];
    json_t *val;
    json_t *cmd_array = NULL;
    const char *cwd = NULL;
    json_t *env = NULL;
    flux_cmd_t *cmd = NULL;
    const char *shell = getenv ("SHELL");

    if (flux_msg_unpack (msg,
                         "{s?o s?o s?s}",
                         "cmd", &cmd_array,
                         "environ", &env,
                         "cwd", &cwd) < 0)
        return NULL;

    if ((cmd_array && !json_is_array (cmd_array))
        || (env && !json_is_object (env))) {
        errno = EPROTO;
        goto err;
    }

    if (!(cmd = flux_cmd_create (0, NULL, env ? NULL: environ)))
        goto err;

    if (cmd_array && json_array_size (cmd_array) > 0) {
        json_array_foreach (cmd_array, i, val) {
            if (flux_cmd_argv_append (cmd, json_string_value (val)) < 0)
                goto err;
        }
    }
    else if (flux_cmd_argv_append (cmd, shell ? shell : "bash") < 0)
        goto err;

    if (!cwd)
        cwd = getcwd (cwd_buf, sizeof (cwd_buf));
    if (flux_cmd_setcwd (cmd, cwd) < 0)
        goto err;

    if (env) {
        const char *key;
        json_t *val;
        json_object_foreach (env, key, val) {
            const char *s = json_string_value (val);
            if (flux_cmd_setenvf (cmd, 1, key, "%s", s) < 0)
                goto err;
        }
    }

    return cmd;
err:
    flux_cmd_destroy (cmd);
    return NULL;
}

static int session_id (struct flux_terminus_server *ts)
{
    unsigned int i = 0;
    while (idset_test (ts->idset, i))
        ++i;
    if (idset_set (ts->idset, i) < 0)
        return -1;
    return i;
}

static char *make_errmsg (char *buf, int buflen, const char *fmt, ...)
{
    va_list ap;
    int saved_errno = errno;
    va_start (ap, fmt);
    vsnprintf (buf, buflen, fmt, ap);
    va_end (ap);
    errno = saved_errno;
    return buf;
}

struct flux_pty *
flux_terminus_server_session_open (struct flux_terminus_server *ts,
                                   int id,
                                   const char *name)
{
    struct terminus_session *s = NULL;

    if (!ts || id < 0 || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (idset_test (ts->idset, id)) {
        errno = EEXIST;
        return NULL;
    }
    if (idset_set (ts->idset, id) < 0)
        goto error;
    if (!(s = terminus_session_create (ts, id, name, false)))
        goto error;
    return s->pty;
error:
    idset_clear (ts->idset, id);
    return NULL;
}

static int check_userid (const flux_msg_t *msg)
{
    uint32_t userid;

    if (flux_msg_get_userid (msg, &userid) < 0)
        return -1;
    if (userid != getuid ()) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static void new_session (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct flux_terminus_server *ts = arg;
    char errbuf [128];
    const char *errmsg = NULL;
    const char *name = NULL;
    flux_cmd_t *cmd;
    int id;
    bool wait = true;
    struct terminus_session *s = NULL;

    if (check_userid (msg) < 0)
        goto error;

    if (flux_request_unpack (msg, NULL,
                             "{s?s}",
                             "name", &name) < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(cmd = make_cmd (msg))) {
        errno = EPROTO;
        errmsg = "failed to parse cmd field";
        goto error;
    }
    if (!name || strlen (name) == 0)
        name = flux_cmd_arg (cmd, 0);
    if ((id = session_id (ts)) < 0) {
        errmsg = "unable to get new session id";
        goto error;
    }
    if (flux_cmd_setenvf (cmd, 1, "FLUX_TERMINUS_SESSION", "%d", id) < 0) {
        errmsg = "failed to set FLUX_TERMINUS_SESSION in environment";
        goto error;
    }
    if (!(s = terminus_session_create (ts, id, name, wait)))
        goto error;
    if (terminus_session_start (s, cmd) < 0) {
        errmsg = make_errmsg (errbuf,
                              sizeof (errbuf),
                              "failed to run %s",
                              flux_cmd_arg (cmd, 0));
        goto error;
    }
    if (flux_respond_pack (h, msg,
                          "{s:s s:s s:i}",
                          "name", name,
                          "pty_service", s->topic,
                          "id", s->id) < 0) {
        llog_error (ts, "flux_respond_pack: %s", strerror (errno));
    }
    flux_cmd_destroy (cmd);
    return;
error:
    /* N.B.: triggers destruction of s
     */
    server_remove_session (ts, s);
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        llog_error (ts, "flux_respond_error: %s", strerror (errno));
}

static int list_append_session (json_t *l, struct terminus_session *s)
{
    json_t *o;

    if (!(o = json_pack ("{s:i s:s s:i s:i s:i s:f}",
                         "id", s->id,
                         "name", s->name,
                         "clients", flux_pty_client_count (s->pty),
                         "pid", flux_subprocess_pid (s->p),
                         "exited", s->exited,
                         "ctime", s->ctime))) {
        errno = ENOMEM;
        return -1;
    }
    if (json_array_append_new (l, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

json_t *server_info (struct flux_terminus_server *ts)
{
    return json_pack ("{s:s s:i s:f}",
                      "service", ts->service,
                      "rank", ts->rank,
                      "ctime", ts->ctime);
}

static void list_sessions (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct flux_terminus_server *ts = arg;
    struct terminus_session *s;
    json_t *sessions = NULL;
    json_t *info = NULL;

    if (check_userid (msg) < 0)
        goto error;

    if (!(sessions = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    s = zlist_first (ts->sessions);
    while (s) {
        if (list_append_session (sessions, s) < 0)
            goto error;
        s = zlist_next (ts->sessions);
    }
    if (!(info = server_info (ts)))
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:o s:o}",
                           "sessions", sessions,
                           "server", info) < 0)
        llog_error (ts, "flux_respond_pack: %s", strerror (errno));
    return;
error:
    json_decref (sessions);
    json_decref (info);
    if (flux_respond_error (h, msg, errno, NULL) < 0)
         llog_error (ts, "flux_respond_error: %s", strerror (errno));
}

static void kill_sessions (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct flux_terminus_server *ts = arg;
    struct terminus_session *s;
    int id;
    int signum;
    int wait = false;
    char *errmsg = NULL;

    if (check_userid (msg) < 0)
        goto error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:i s?i}",
                             "id", &id,
                             "signal", &signum,
                             "wait", &wait) < 0)
        goto error;
    if (!(s = session_lookup (ts, id))) {
        errno = ENOENT;
        goto error;
    }
    if (terminus_session_kill (s, signum) < 0)
        goto error;
    /*
     *  If 'wait' flag was specified, then attach a new client to pty
     *   that will respond once the pty has fully exited. O/w simply
     *   respond with Success now.
     */
    if (wait) {
        if (flux_pty_add_exit_watcher (s->pty, msg) < 0)
            goto error;
    }
    else if (flux_respond (ts->h, msg, NULL) < 0)
        llog_error (ts, "flux_respond: %s", strerror (errno));
    return;
error:
    if (flux_respond_error (ts->h, msg, errno, errmsg) < 0)
        llog_error (ts, "flux_respond_error: %s", strerror (errno));
}

static void kill_server_exit (struct flux_terminus_server *ts, void *arg)
{
    const flux_msg_t *msg = arg;
    flux_respond (ts->h, msg, NULL);
    flux_msg_decref (msg);
    flux_terminus_server_stop (ts);
}

static void kill_server (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct flux_terminus_server *ts = arg;
    struct terminus_session *s;

    if (check_userid (msg) < 0)
        goto error;

    /*  If no active sessions, exit server immediately */
    if (zlist_size (ts->sessions) == 0) {
        kill_server_exit (ts, (void *) flux_msg_incref (msg));
        return;
    }

    /*  Grab reference to message so we can notify when all sessions
     *   have been killed
     */
    flux_msg_incref (msg);

    /*  Register empty callback so that server is stopped and response
     *   goes out when last session exits.
     */
    if (flux_terminus_server_notify_empty (ts,
                                           kill_server_exit,
                                           (void *) msg) < 0) {
        flux_msg_decref (msg);
        goto error;
    }

    /*  Kill all active sessions */
    s = zlist_first (ts->sessions);
    while (s) {
        (void) terminus_session_kill (s, SIGKILL);
        s = zlist_next (ts->sessions);
    }
    return;
error:
    if (flux_respond_error (ts->h, msg, errno, NULL) < 0)
        llog_error (ts, "flux_respond_error: %s", strerror (errno));
}

static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct flux_terminus_server *ts = arg;
    struct terminus_session *s;
    const char *sender;

    if (!(sender = flux_msg_route_first (msg))) {
        llog_error (ts, "flux_msg_get_route_first: uuid is NULL!");
        return;
    }
    s = zlist_first (ts->sessions);
    while (s) {
        flux_pty_disconnect_client (s->pty, sender);
        s = zlist_next (ts->sessions);
    }
}

static const struct flux_msg_handler_spec handler_tab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "list",
        list_sessions,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "new",
        new_session,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kill",
        kill_sessions,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "kill-server",
        kill_server,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "disconnect",
        disconnect_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static inline int
msg_handler_count (const struct flux_msg_handler_spec htab[])
{
    int count = 0;
    while (htab[count].topic_glob)
        count++;
    return count;
}

static int start_msghandlers (struct flux_terminus_server *ts,
                              const struct flux_msg_handler_spec tab[])
{
    int i;
    char topic[128];
    int len = sizeof (topic);
    struct flux_match match = FLUX_MATCH_REQUEST;
    int count = msg_handler_count (tab);

    ts->handlers = calloc (count+1, sizeof (*ts->handlers));
    for (i = 0; i < count; i++) {
        flux_msg_handler_t *mh = NULL;
        if (snprintf (topic,
                      len,
                      "%s.%s", ts->service,
                      tab[i].topic_glob) >= len)
            goto error;
        match.topic_glob = topic;
        match.typemask = tab[i].typemask;
        if (!(mh = flux_msg_handler_create (ts->h, match, tab[i].cb, ts)))
            goto error;
        flux_msg_handler_allow_rolemask (mh, tab[i].rolemask);
        flux_msg_handler_start (mh);
        ts->handlers[i] = mh;
    }
    return 0;
error:
    flux_msg_handler_delvec (ts->handlers);
    return -1;
}

flux_future_t *
flux_terminus_server_unregister (struct flux_terminus_server *ts)
{
    return flux_service_unregister (ts->h, ts->service);
}

struct flux_terminus_server *
flux_terminus_server_create (flux_t *h, const char *service)
{
    struct flux_terminus_server *ts;

    if (!h || !service) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ts = calloc (1, sizeof (*ts)))
        || !(ts->empty_waiters = zlist_new ())
        || !(ts->sessions = zlist_new ())) {
        goto err;
    }
    ts->h = h;
    ts->ctime = flux_reactor_now (flux_get_reactor (h));

    /*  In test mode, avoid flux_get_rank(3) as it will hang
     */
    if (getenv ("FLUX_TERMINUS_TEST_SERVER"))
        ts->rank = -1;
    else if (flux_get_rank (h, &ts->rank) < 0)
        goto err;

    if (!(ts->idset = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto err;

    if (strlen (service) > sizeof (ts->service) - 1)
        goto err;
    strcpy (ts->service, service);

    if (start_msghandlers (ts, handler_tab) < 0)
        goto err;

    return ts;
err:
    flux_terminus_server_destroy (ts);
    return NULL;
}

void flux_terminus_server_set_log (struct flux_terminus_server *ts,
                                   terminus_log_f log_fn,
                                   void *log_arg)
{
    if (ts) {
        ts->llog = log_fn;
        ts->llog_data = log_arg;
    }
}

/* vi: ts=4 sw=4 expandtab
 */

