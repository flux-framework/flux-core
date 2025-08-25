/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <termios.h>

#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/llog.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "pty.h"

static struct termios orig_term;
static bool term_needs_restoration = false;

struct exit_waiter {
    flux_pty_client_exit_f cb;
    void *arg;
};

struct flux_pty_client {
    flux_t *h;

    pty_log_f llog;
    void *llog_data;

    int flags;
    int rank;
    char *service;
    bool attached;

    flux_watcher_t *fdw;  /* fd watcher for STDIN */
    flux_watcher_t *sw;   /* signal watcher       */
    flux_watcher_t *kaw;  /* keepalive timer      */

    flux_future_t *rpc_f; /* streaming RPC future */

    struct termios term;

    zlist_t *exit_waiters;
    int wait_status;
    char *exit_message;
};

static int get_winsize (struct winsize *ws)
{
    return ioctl (STDIN_FILENO, TIOCGWINSZ, ws);
}

void flux_pty_client_destroy (struct flux_pty_client *c)
{
    if (c) {
        int saved_errno = errno;
        flux_watcher_destroy (c->fdw);
        flux_watcher_destroy (c->sw);
        flux_watcher_destroy (c->kaw);
        flux_future_destroy (c->rpc_f);
        zlist_destroy (&c->exit_waiters);
        free (c->exit_message);
        free (c->service);
        free (c);
        errno = saved_errno;
    }
}

int flux_pty_client_exit_status (struct flux_pty_client *c,
                                 int *statusp)
{
    if (!c || !statusp) {
        errno = EINVAL;
        return -1;
    }
    *statusp = c->wait_status;
    return 0;
}

struct flux_pty_client *flux_pty_client_create (void)
{
    struct flux_pty_client *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;
    c->exit_waiters = zlist_new ();
    if (!c->exit_waiters) {
        flux_pty_client_destroy (c);
        return NULL;
    }
    return c;
}

static void notify_exit (struct flux_pty_client *c)
{
    struct exit_waiter *w;
    while ((w = zlist_pop (c->exit_waiters))) {
        (*w->cb) (c, w->arg);
        free (w);
    }
}

int flux_pty_client_notify_exit (struct flux_pty_client *c,
                                 flux_pty_client_exit_f fn,
                                 void *arg)
{
    struct exit_waiter *w;

    if (!c || !fn) {
        errno = EINVAL;
        return -1;
    }
    w = calloc (1, sizeof (*w));
    if (!w)
        return -1;
    w->cb = fn;
    w->arg = arg;
    if (zlist_append (c->exit_waiters, w) < 0) {
        free (w);
        errno = ENOMEM;
        return -1;
    }
    zlist_freefn (c->exit_waiters, w, (zlist_free_fn *) free, true);
    return 0;
}

static int invalid_flags (int flags)
{
    const int valid_flags =
        FLUX_PTY_CLIENT_ATTACH_SYNC
        | FLUX_PTY_CLIENT_CLEAR_SCREEN
        | FLUX_PTY_CLIENT_NOTIFY_ON_ATTACH
        | FLUX_PTY_CLIENT_NOTIFY_ON_DETACH
        | FLUX_PTY_CLIENT_NORAW
        | FLUX_PTY_CLIENT_STDIN_PIPE;
    return (flags & ~valid_flags);
}

int flux_pty_client_set_flags (struct flux_pty_client *c, int flags)
{
    if (!c || invalid_flags (flags)) {
        errno = EINVAL;
        return -1;
    }
    c->flags = flags;
    return 0;
}

int flux_pty_client_get_flags (struct flux_pty_client *c)
{
    if (!c) {
        errno = EINVAL;
        return -1;
    }
    return c->flags;
}

void flux_pty_client_set_log (struct flux_pty_client *c,
                              pty_log_f log,
                              void *log_data)
{
    if (c) {
        c->llog = log;
        c->llog_data = log_data;
    }
}

static void flux_pty_client_stop (struct flux_pty_client *c)
{
    flux_watcher_stop (c->fdw);
    flux_watcher_stop (c->sw);
    flux_watcher_stop (c->kaw);
}

static int flux_pty_client_set_server (struct flux_pty_client *c,
                                       int rank,
                                       const char *service)
{
    if (!(c->service = strdup (service)))
        return -1;
    c->rank = rank;
    return 0;
}

static void cls (void)
{
    /* ANSI clear screen + Home */
    printf ("\033[2J\033[;H");
    fflush (stdout);
}

void flux_pty_client_restore_terminal (void)
{
    if (term_needs_restoration) {
        tcsetattr (STDIN_FILENO, TCSADRAIN, &orig_term);
        /* https://en.wikipedia.org/wiki/ANSI_escape_code
         * Best effort: attempt to ensure cursor is visible:
         */
        printf ("\033[?25h");
        fflush (stdout);
        term_needs_restoration = false;
    }
}

static int setup_terminal (struct flux_pty_client *c)
{
    if (tcgetattr (STDIN_FILENO, &orig_term) < 0)
        return -1;

    c->term = orig_term;

    cfmakeraw (&c->term);
    c->term.c_cc[VLNEXT] = _POSIX_VDISABLE;
    c->term.c_cc[VMIN] = 1;
    c->term.c_cc[VTIME] = 0;
    if (tcsetattr (STDIN_FILENO, TCSANOW, &c->term) < 0) {
        llog_warning (c, "failed to setup terminal\n");
        return -1;
    }
    term_needs_restoration = true;
    atexit (flux_pty_client_restore_terminal);
    return 0;
}

static void pty_client_attached (struct flux_pty_client *c)
{
    /*  Setup terminal, start watching stdin for data */
    if (!(c->flags & FLUX_PTY_CLIENT_NORAW))
        (void) setup_terminal (c);
    if (c->flags & FLUX_PTY_CLIENT_CLEAR_SCREEN)
        cls ();
    if (c->flags & FLUX_PTY_CLIENT_NOTIFY_ON_ATTACH) {
        printf ("[attached]\r\n");
    }
    flux_watcher_start (c->fdw);
    flux_watcher_start (c->kaw);
    if (!(c->flags & FLUX_PTY_CLIENT_STDIN_PIPE))
        flux_watcher_start (c->sw);
    c->attached = true;
}

static void pty_client_data (struct flux_pty_client *c, flux_future_t *f)
{
    const flux_msg_t *msg;
    char *data;
    size_t len;
    flux_error_t error;

    if (flux_future_get (f, (const void **)&msg) < 0) {
        llog_error (c, "data response: %s", future_strerror (f, errno));
        return;
    }
    if (pty_data_unpack (msg, &error, &data, &len) < 0) {
        llog_error (c, "unpack: %s", error.text);
        return;
    }
    for (int n = 0; n < len; ) {
        int ret = write (STDOUT_FILENO, data + n, len - n);
        if (ret < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK )
                continue; // try again
            llog_error (c, "write %zu bytes: %s", len, strerror (errno));
            return;
        } else {
            n += ret;
        }
    }
}

static void client_resize_cb (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0) {
        struct flux_pty_client *c = flux_future_aux_get (f, "pty_client");
        if (c)
            llog_error (c, "resize: %s", future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

/*  Server requested winsize
 */
static void pty_client_resize (struct flux_pty_client *c)
{
    flux_future_t *f;
    struct winsize ws;
    if (get_winsize (&ws) < 0) {
        llog_error (c, "get winsize failed: %s", strerror (errno));
        return;
    }
    if (!(f = flux_rpc_pack (c->h, c->service, c->rank, 0,
                             "{s:s s:{s:i s:i}}",
                             "type", "resize",
                             "winsize",
                               "rows", ws.ws_row,
                               "cols", ws.ws_col))) {
        llog_error (c, "flux_rpc_pack type=resize: %s", flux_strerror (errno));
        return;
    }
    (void) flux_future_aux_set (f, "pty_client", c, NULL);
    if (flux_future_then (f, -1., client_resize_cb, c) < 0)
        llog_error (c, "flux_future_then: %s", flux_strerror (errno));
}

static void pty_die (struct flux_pty_client *c, int code, const char *message)
{
    flux_pty_client_stop (c);
    /*  Only overwrite c->wait_status for code > 0. O/w, we collect the
     *   actual task exit status
     */
    if (code)
        c->wait_status = code << 8;
    if (message) {
        free (c->exit_message);
        c->exit_message = strdup (message);
    }
    if (c->attached && (c->flags & FLUX_PTY_CLIENT_NOTIFY_ON_DETACH)) {
        printf ("\033[999H[detached: %s]\033[K\n\r",
                c->exit_message ? c->exit_message : "unknown reason");
        fflush (stdout);
    }
    notify_exit (c);
}

static void pty_client_exit (struct flux_pty_client *c, flux_future_t *f)
{
    const char *message;

    if (flux_rpc_get_unpack (f, "{s:s s:i}",
                                "message", &message,
                                "status", &c->wait_status) < 0) {
        llog_error (c, "rpc unpack: %s", future_strerror (f, errno));
        message="unknown reason";
    }
    free (c->exit_message);
    c->exit_message = strdup (message);
    flux_pty_client_stop (c);
}

static void pty_server_cb (flux_future_t *f, void *arg)
{
    struct flux_pty_client *c = arg;
    const char *type;
    if (flux_rpc_get_unpack (f, "{s:s}", "type", &type) < 0) {
        const char *message = NULL;
        int code = 1;
        if (errno == ENOSYS)
            message = "No such session";
        else if (errno != ENODATA)
            message = future_strerror (f, errno);
        else
            code = 0;
        pty_die (c, code, message);
        flux_future_destroy (c->rpc_f);
        c->rpc_f = NULL;
        return;
    }
    if (streq (type, "attach"))
        pty_client_attached (c);
    else if (streq (type, "data"))
        pty_client_data (c, f);
    else if (streq (type, "resize"))
        pty_client_resize (c);
    else if (streq (type, "exit"))
        pty_client_exit (c, f);
    else {
        llog_error (c, "unknown server response type=%s", type);
        pty_die (c, 1, "Protocol error");
        flux_future_destroy (c->rpc_f);
        c->rpc_f = NULL;
        return;
    }
    flux_future_reset (f);
}

int flux_pty_client_detach (struct flux_pty_client *c)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (c->h, c->service, c->rank, 0,
                             "{s:s}", "type", "detach"))) {
        llog_error (c, "flux_rpc_pack: %s", flux_strerror (errno));
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static void data_write_cb (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0) {
        struct flux_pty_client *c = flux_future_aux_get (f, "pty_client");
        if (c) {
            flux_pty_client_detach (c);
            if (errno == ENOSYS)
                pty_die (c, 1, "remote pty disappeared");
            else
                pty_die (c, 1, "error writing to remote pty");
        }
    }
    flux_future_destroy (f);
}

flux_future_t *flux_pty_client_write (struct flux_pty_client *c,
                                      const void *buf,
                                      ssize_t len)
{
    flux_future_t *f;
    json_t *o = NULL;

    if (!c || !buf || len < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(o = pty_data_encode (buf, len)) )
        return NULL;
    f = flux_rpc_pack (c->h, c->service, c->rank, 0, "O", o);
    ERRNO_SAFE_WRAP (json_decref, o);
    return f;
}

static void pty_read_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct flux_pty_client *c = arg;
    flux_future_t *f;
    char buf[4096];
    ssize_t len;

    if ((len = read (STDIN_FILENO, buf, sizeof (buf))) < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            llog_fatal (c, "read: %s", strerror (errno));
            pty_client_exit (c, NULL);
        }
        return;
    }
    if (len == 0) {
        flux_pty_client_stop (c);
        if (c->flags & FLUX_PTY_CLIENT_STDIN_PIPE)
            flux_pty_client_detach (c);
        return;
    }
    if (buf[0] == '') { /* request detach */
        (void ) flux_pty_client_detach (c);
        return;
    }
    if (!(f = flux_pty_client_write (c, buf, len))) {
        llog_error (c, "flux_pty_client_write: %s", strerror (errno));
        return;
    }
    (void) flux_future_aux_set (f, "pty_client", c, NULL);
    if (flux_future_then (f, -1., data_write_cb, c) < 0) {
        llog_error (c, "flux_future_then: %s", strerror (errno));
        flux_future_destroy (f);
    }
}

static void sigwinch_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    pty_client_resize ((struct flux_pty_client *)arg);
}

static void keepalive_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct flux_pty_client *c = arg;
    flux_future_t *f;
    char *buf = "";

    if (!(f = flux_pty_client_write (c, buf, 0))) {
        llog_error (c, "flux_pty_client_write: %s", strerror (errno));
        return;
    }
    (void) flux_future_aux_set (f, "pty_client", c, NULL);
    if (flux_future_then (f, -1., data_write_cb, c) < 0) {
        llog_error (c, "flux_future_then: %s", strerror (errno));
        flux_future_destroy (f);
    }
}

bool flux_pty_client_attached (struct flux_pty_client *c)
{
    return c && c->attached;
}

int flux_pty_client_attach (struct flux_pty_client *c,
                            flux_t *h,
                            int rank,
                            const char *service)
{
    flux_future_t *f;
    struct winsize ws;
    const char *mode;

    if (!c || !h || !service) {
        errno = EINVAL;
        return -1;
    }

    if (isatty (STDIN_FILENO) && get_winsize (&ws) < 0)
        return -1;

    /*  In some test conditions, get_winsize returns 0 for
     *   rows or columns. Fix that here. It shouldn't be an
     *   issue with a real tty.
     */
    if (ws.ws_row <= 0)
        ws.ws_row = 1;
    if (ws.ws_col <= 0)
        ws.ws_col = 1;

    if (flux_pty_client_set_server (c, rank, service) < 0)
        return -1;

    c->h = h;

    c->fdw = flux_fd_watcher_create (flux_get_reactor (h),
                                     STDIN_FILENO,
                                     FLUX_POLLIN,
                                     pty_read_cb,
                                     c);
    c->sw = flux_signal_watcher_create (flux_get_reactor (h),
                                        SIGWINCH,
                                        sigwinch_cb,
                                        c);
    c->kaw = flux_timer_watcher_create (flux_get_reactor (h),
                                        1., 1.,
                                        keepalive_cb,
                                        c);
    if (!c->fdw || !c->sw || !c->kaw)
        return -1;

    mode = c->flags & FLUX_PTY_CLIENT_STDIN_PIPE ? "wo" : "rw";

    if (!(f = flux_rpc_pack (h, service, rank, FLUX_RPC_STREAMING,
                             "{s:s s:s s:{s:i s:i}}",
                             "type", "attach",
                             "mode", mode,
                             "winsize",
                              "rows", ws.ws_row,
                              "cols", ws.ws_col))) {
            llog_error (c, "flux_rpc_pack: %s", flux_strerror (errno));
            return -1;
    }
    if (c->flags & FLUX_PTY_CLIENT_ATTACH_SYNC) {
        const char *type;
        if (flux_rpc_get_unpack (f, "{s:s}", "type", &type) < 0
            || !streq (type, "attach")) {
            flux_future_destroy (f);
            return -1;
        }
        pty_client_attached (c);
        flux_future_reset (f);
    }
    if (flux_future_then (f, -1, pty_server_cb, c) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    c->rpc_f = f;
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */

