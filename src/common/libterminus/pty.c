/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*  pseudoterminal multiplexer like dtach, but using flux service
 *   endpoint and messages rather than unix domain sockets.
 *
 *   Run and attach to a process anywhere in your Flux instance.
 *
 *  PROTOCOL:
 *
 *  Client attach to server:
 *  { "type":"attach", "mode":s, "winsize":{"rows":i,"columns":i}}
 *  where mode is one of "rw", "ro", or "rw"
 *
 *  Server response to attach:
 *  { "type":"attach" }
 *
 *  Resize request: (client->server or server->client)
 *  { "type":"resize", "winsize"?{"rows":i,"columns":i} }
 *
 *  Client/server write raw data to tty (string is utf-8)
 *  { "type":"data", "data":s% }
 *
 *  Client detach:
 *  { "type":"detach" }
 *
 *  Server tell client to exit (if process exited, include exit status):
 *  { "type":"exit", "message":s, "status":i }
 *
 *  ENODATA: End of streaming RPC
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "ccan/base64/base64.h"

#define LLOG_SUBSYSTEM "pty"
#include "src/common/libutil/llog.h"

#include "pty.h"

struct pty_client {
    char *uuid;
    const flux_msg_t *req;

    bool write_enabled;
    bool read_enabled;
};

struct flux_pty {
    flux_t *h;

    pty_log_f llog;
    void *llog_data;

    int leader;
    char *follower;
    flux_watcher_t *fdw;

    bool wait_for_client;
    bool wait_on_close;
    bool exited;
    int status;

    zlist_t *clients;

    pty_monitor_f monitor;
    pty_complete_f complete;
    struct aux_item *aux;
};

static void pty_client_destroy (struct pty_client *c)
{
    if (c) {
        int saved_errno = errno;
        flux_msg_decref (c->req);
        free (c->uuid);
        free (c);
        errno = saved_errno;
    }
}

static struct pty_client *pty_client_create (const flux_msg_t *msg)
{
    const char *uuid;
    struct pty_client *c = NULL;

    if (!(c = calloc (1, sizeof (*c))))
        return NULL;
    if (!(uuid = flux_msg_route_first (msg))) {
        errno = EPROTO;
        goto error;
    }
    c->req = flux_msg_incref (msg);
    if (!(c->uuid = strdup (uuid)))
        goto error;
    return c;
error:
    pty_client_destroy (c);
    return NULL;
}

static struct pty_client *pty_client_find (struct flux_pty *pty,
                                           const char *uuid)
{
    struct pty_client *c = zlist_first (pty->clients);
    while (c) {
        if (streq (c->uuid, uuid))
            break;
        c = zlist_next (pty->clients);
    }
    return c;
}

static struct pty_client *pty_client_find_sender (struct flux_pty *pty,
                                                  const flux_msg_t *msg)
{
    struct pty_client *c = NULL;
    const char *uuid;

    if (!(uuid = flux_msg_route_first (msg))) {
        llog_error (pty, "flux_msg_get_route_first: uuid is NULL!");
        return NULL;
    }
    c = pty_client_find (pty, uuid);

    return c;
}

static int pty_client_send_exit (struct flux_pty *pty,
                                 struct pty_client *c,
                                 const char *message,
                                 int status)
{
    if (flux_respond_pack (pty->h, c->req,
                              "{s:s s:s? s:i}",
                              "type", "exit",
                              "message", message,
                              "status", status) < 0)
        return -1;
    /* End of stream */
    return flux_respond_error (pty->h, c->req, ENODATA, NULL);
}

static int pty_clients_notify_exit (struct flux_pty *pty)
{
    struct pty_client *c = zlist_first (pty->clients);
    while (c) {
        if (pty_client_send_exit (pty, c, "session exiting", pty->status) < 0)
            llog_error (pty, "send_exit: %s", flux_strerror (errno));
        c = zlist_next (pty->clients);
    }
    return 0;
}

static int pty_client_detach (struct flux_pty *pty, struct pty_client *c)
{
    if (c) {
        pty_client_send_exit (pty, c, "Client requested detach", 0);
        zlist_remove (pty->clients, c);
        pty_client_destroy (c);
    }
    /* XXX: Resize remaining clients? */
    return 0;
}

int flux_pty_disconnect_client (struct flux_pty *pty, const char *sender)
{
    if (!pty || !sender) {
        errno = EINVAL;
        return -1;
    }
    return pty_client_detach (pty, pty_client_find (pty, sender));
}

int flux_pty_kill (struct flux_pty *pty, int sig)
{
    pid_t pgrp = -1;
    if (!pty || sig <= 0) {
        errno = EINVAL;
        return -1;
    }
    /*  Disable wait-on-client if being killed (except for terminal resize) */
    if (sig != SIGWINCH) {
        pty->wait_for_client = false;
        pty->wait_on_close = false;
    }
#ifdef TIOCSIG
    if (ioctl (pty->leader, TIOCSIG, sig) >= 0)
        return 0;
    llog_debug (pty, "ioctl (TIOCSIG): %s", strerror (errno));
#endif
    if (ioctl (pty->leader, TIOCGPGRP, &pgrp) >= 0
        && pgrp > 0
        && kill (-pgrp, sig) >= 0)
        return 0;
    llog_debug (pty, "ioctl (TIOCPGRP): %s", strerror (errno));
    return -1;
}

static void pty_clients_destroy (struct flux_pty *pty)
{
    struct pty_client *c = zlist_first (pty->clients);
    while (c) {
        pty_client_destroy (c);
        c = zlist_next (pty->clients);
    }
}

void flux_pty_destroy (struct flux_pty *pty)
{
    if (pty) {
        flux_watcher_destroy (pty->fdw);
        pty_clients_notify_exit (pty);
        pty_clients_destroy (pty);
        zlist_destroy (&pty->clients);
        if (pty->leader >= 0)
            close (pty->leader);
        free (pty->follower);
        aux_destroy (&pty->aux);
        free (pty);
    }
}

static void check_pty_complete (struct flux_pty *pty)
{
    llog_debug (pty, "wait_for_client=%d wait_on_close=%d exited=%d",
                pty->wait_for_client, pty->wait_on_close, pty->exited);
    if (!pty->wait_for_client
        && !pty->wait_on_close
        && pty->exited)
        (*pty->complete) (pty);
}

void flux_pty_exited (struct flux_pty *pty, int status)
{
    if (pty) {
        pty->status = status;
        pty->exited = true;
        check_pty_complete (pty);
    }
}

static struct flux_pty * flux_pty_create ()
{
    struct flux_pty *pty = calloc (1, sizeof (*pty));
    if (!pty)
        return NULL;
    pty->leader = -1;
    pty->clients = zlist_new ();
    return pty;
}

int flux_pty_client_count (struct flux_pty *pty)
{
    if (pty) {
        return zlist_size (pty->clients);
    }
    return 0;
}

void flux_pty_wait_for_client (struct flux_pty *pty)
{
    if (pty)
        pty->wait_for_client = true;
}

void flux_pty_wait_on_close (struct flux_pty *pty)
{
    if (pty)
        pty->wait_on_close = true;
}

struct flux_pty * flux_pty_open ()
{
    struct flux_pty *pty = flux_pty_create ();
    const char *name;
    struct winsize ws = { 0 };
    if (!pty)
        return NULL;
    if ((pty->leader = posix_openpt (O_RDWR | O_NOCTTY |O_CLOEXEC)) < 0
        || grantpt (pty->leader) < 0
        || unlockpt (pty->leader) < 0
        || !(name = ptsname (pty->leader))
        || !(pty->follower = strdup (name)))
        goto err;

    /*  Set a default winsize, so it isn't 0,0 */
    ws.ws_row = 25;
    ws.ws_col = 80;
    if (ioctl (pty->leader, TIOCSWINSZ, &ws) < 0)
        goto err;

    pty->complete = flux_pty_destroy;

    return pty;
err:
    flux_pty_destroy (pty);
    return NULL;
}

void flux_pty_set_log (struct flux_pty *pty,
                       pty_log_f log,
                       void *log_data)
{
    if (pty) {
        pty->llog = log;
        pty->llog_data = log_data;
    }
}

void flux_pty_monitor (struct flux_pty *pty, pty_monitor_f fn)
{
    if (!pty)
        return;

    pty->monitor = fn;
    /*  If a monitor function is provided, and there are currently no
     *   other clients, ensure the pty fd_watcher is started.
     */
    if (fn != NULL
        && !pty->wait_for_client
        && zlist_size (pty->clients) == 0) {
        flux_watcher_start (pty->fdw);
    }
}

void flux_pty_set_complete_cb (struct flux_pty *pty, pty_complete_f fn)
{
    if (pty)
        pty->complete = fn;
}

int flux_pty_leader_fd (struct flux_pty *pty)
{
    if (!pty) {
        errno = EINVAL;
        return -1;
    }
    return pty->leader;
}

const char *flux_pty_name (struct flux_pty *pty)
{
    if (!pty) {
        errno = EINVAL;
        return NULL;
    }
    return pty->follower;
}

int flux_pty_attach (struct flux_pty *pty)
{
    int fd;
    if (!pty || !pty->follower) {
        errno = EINVAL;
        return -1;
    }
    if ((fd = open (pty->follower, O_RDWR|O_NOCTTY)) < 0)
        return -1;

    /*  New session so we can attach this process to tty */
    (void) setsid ();

    /*  Make the follower pty (known in documentation as the slave
     *  side of pty) our controlling terminal */
    if (ioctl (fd, TIOCSCTTY, NULL) < 0)
        return -1;

    /*  dup pty/in/out onto tty fd */
    if (dup2 (fd, STDIN_FILENO) != STDIN_FILENO
        || dup2 (fd, STDOUT_FILENO) != STDOUT_FILENO
        || dup2 (fd, STDERR_FILENO) != STDERR_FILENO) {
        llog_error (pty, "dup2: %s", strerror (errno));
        return -1;
    }
    if (fd > 2)
        (void) close (fd);
    if (pty->leader >= 0)
        (void) close (pty->leader);
    return 0;
}

static int encode_base64 (const char *data, int len, char **destp, int *lenp)
{
    ssize_t n;
    char *dest = NULL;
    size_t destlen = base64_encoded_length (len) + 1;

    if (!(dest = malloc (destlen))
        || ((n = base64_encode (dest, destlen, data, len)) < 0))
        return -1;
    *destp = dest;
    *lenp = n;
    return 0;
}

static json_t *pty_data_encode_base64 (const void *data, int len)
{
    json_t *o = NULL;
    char *b64data = NULL;
    int b64len = 0;

    if (encode_base64 (data, len, &b64data, &b64len) < 0)
        return NULL;

    o = json_pack ("{s:s s:s s:s#}",
                   "type", "data",
                   "encoding", "base64",
                   "data", b64data, b64len);

    ERRNO_SAFE_WRAP (free, b64data);
    return o;
}

json_t *pty_data_encode (const void *data, int len)
{
    json_t *o;

    if (!(o = json_pack ("{s:s s:s#}",
                         "type", "data",
                         "data", (char *) data, len))) {
        /*
         *  json_pack() may fail if there are bytes that cannot be encoded
         *  as UTF-8, e.g. U+0000 is specifically not allowed in jansson.
         *  Try encoding as base64 instead.
         */
        o = pty_data_encode_base64 (data, len);
    }
    return o;
}

void pty_client_send_data (struct flux_pty *pty, void *data, int len)
{
    struct pty_client *c = zlist_first (pty->clients);

    if (pty->monitor)
        (*pty->monitor) (pty, data, len);

    while (c) {
        if (c->read_enabled) {
            json_t *o = pty_data_encode (data, len);
            if (!o || flux_respond_pack (pty->h, c->req, "O", o) < 0)
                llog_error (pty, "send data: %s", strerror (errno));
            json_decref (o);
        }
        c = zlist_next (pty->clients);
    }
}

void pty_client_monitor_send_eof (struct flux_pty *pty)
{
    if (pty->monitor)
        (*pty->monitor) (pty, NULL, 0);
}

static void pty_read (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct flux_pty *pty = arg;
    ssize_t n;
    char buf [4096];

    /* XXX: notify all clients and exit */
    if (revents & FLUX_POLLERR)
        return;

    n = read (pty->leader, buf, sizeof (buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR)
            return;
        /*
         *  pty: EIO indicates pty follower has closed.
         *   stop the fd watcher, disable wait_on_close and
         *   check to see if the pty has met all conditions
         *   for completion:
         */
        if (errno == EIO) {
            flux_watcher_stop (pty->fdw);
            pty->wait_on_close = false;
            check_pty_complete (pty);
            pty_client_monitor_send_eof (pty);
            return;
        }
        llog_error (pty, "read: %s", strerror (errno));
        return;
    }
    else if (n > 0)
        pty_client_send_data (pty, buf, n);
}

static int pty_resize (struct flux_pty *pty, const flux_msg_t *msg)
{
    struct winsize ws = { 0 };

    if (flux_msg_unpack (msg,
                         "{s:{s:i s:i}}",
                         "winsize",
                         "rows", &ws.ws_row,
                         "cols", &ws.ws_col) < 0) {
        llog_error (pty, "msg_unpack failed: %s", strerror (errno));
        return -1;
    }
    llog_debug (pty, "resize: %dx%d", ws.ws_row, ws.ws_col);
    if (ws.ws_row <= 0 || ws.ws_col <= 0) {
        errno = EINVAL;
        llog_error (pty, "bad resize: row=%d, col=%d", ws.ws_row, ws.ws_col);
        return -1;
    }
    if (ioctl (pty->leader, TIOCSWINSZ, &ws) < 0) {
        llog_error (pty, "ioctl: TIOCSWINSZ: %s", strerror (errno));
        return -1;
    }

    /*  notify foreground process to redraw (best effort) */
    (void) flux_pty_kill (pty, SIGWINCH);

    return 0;
}

int flux_pty_add_exit_watcher (struct flux_pty *pty, const flux_msg_t *msg)
{
    struct pty_client *c = pty_client_create (msg);
    if (!c)
        goto err;
    if (zlist_append (pty->clients, c) < 0) {
        errno = ENOMEM;
        goto err;
    }
    return 0;
err:
    pty_client_destroy (c);
    return -1;
}

static int pty_client_set_mode (struct flux_pty *pty,
                                struct pty_client *c,
                                const flux_msg_t *msg)
{
    const char *mode;
    if (flux_msg_unpack (msg, "{s:s}", "mode", &mode) < 0)
        return -1;
    /*  Valid modes are currently only "ro", "wo", "rw" */
    if (streq (mode, "rw"))
        c->read_enabled = c->write_enabled = true;
    else if (streq (mode, "wo"))
        c->write_enabled = true;
    else if (streq (mode, "ro"))
        c->read_enabled = true;
    else {
        llog_error (pty, "client=%s: invalid mode: %s", c->uuid, mode);
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int pty_attach (struct flux_pty *pty, const flux_msg_t *msg)
{
    int saved_errno;
    struct pty_client *c = pty_client_create (msg);

    if (!c)
        goto err;
    if (pty_client_set_mode (pty, c, msg) < 0)
        goto err;

    /*  Only start watching tty fd when first client attaches */
    if (zlist_size (pty->clients) == 0 && c->read_enabled) {
        flux_watcher_start (pty->fdw);
        /* Done waiting for first client */
        pty->wait_for_client = false;
    }
    if (zlist_append (pty->clients, c) < 0) {
        errno = ENOMEM;
        goto err;
    }
    if (c->read_enabled
        && c->write_enabled
        && pty_resize (pty, msg) < 0)
        goto err;
    if (flux_respond_pack (pty->h, msg, "{s:s}", "type", "attach") < 0)
        goto err;
    return 0;
err:
    saved_errno = errno;
    if (c)
        zlist_remove (pty->clients, c);
    pty_client_destroy (c);
    errno = saved_errno;
    return -1;
}

static int decode_base64 (char *src,
                          size_t srclen,
                          char **datap,
                          size_t *lenp)
{
    ssize_t rc;
    char *data;
    size_t size = base64_decoded_length (srclen) + 1;

    if (!(data = malloc (size)))
        return -1;
    if ((rc = base64_decode (data, size, src, srclen)) < 0) {
        ERRNO_SAFE_WRAP (free, data);
        return -1;
    }
    *lenp = rc;
    *datap = data;
    return 0;
}

int pty_data_unpack (const flux_msg_t *msg,
                     flux_error_t *errp,
                     char **datap,
                     size_t *lenp)
{
    const char *encoding = NULL;
    void *data;
    size_t len;
    json_t *o;
    json_error_t error;

    /* Note: Use json_unpack_ex(3) in order to pass the JSON_ALLOW_NUL flag.
     *  This is necessary since pty data packed with 's#' may contain NUL
     *  characters, especially in the case of ^@/Ctrl-Space/etc, which is
     *  encoded as a NUL character (empty string with len=1).
     */
    if (flux_msg_unpack (msg, "o", &o) < 0)
        return errprintf (errp,
                          "failed to unpack data msg: %s",
                          strerror (errno));
    if (json_unpack_ex (o,
                        NULL,
                        JSON_ALLOW_NUL,
                        "{s:s% s?s}",
                        "data", &data, &len,
                        "encoding", &encoding) < 0) {
        errno = EPROTO;
        return errprintf (errp,
                          "failed to unpack data msg: %s",
                          error.text);
    }
    if (encoding) {
        if (streq (encoding, "base64")) {
            char *b64data = NULL;
            size_t b64len;
            if (decode_base64 (data, len, &b64data, &b64len) < 0) {
                return errprintf (errp,
                                  "failed to decode %zu bytes of base64",
                                  len);
            }
            if (flux_msg_aux_set (msg, NULL, b64data, free) < 0) {
                ERRNO_SAFE_WRAP (free, b64data);
                return errprintf (errp,
                                  "flux_msg_aux_set: %s",
                                  strerror (errno));
            }
            data = b64data;
            len = b64len;
        }
        else {
            errno = EPROTO;
            return errprintf (errp,
                              "invalid pty data encoding: %s",
                              encoding);
        }
    }
    *datap = data;
    *lenp = len;
    return 0;
}

static int pty_write (struct flux_pty *pty, const flux_msg_t *msg)
{
    char *data;
    size_t len;
    flux_error_t error;

    if (pty_data_unpack (msg, &error, &data, &len) < 0) {
        llog_error (pty, "%s", error.text);
        return -1;
    }
    if (write (pty->leader, data, len) < 0) {
        llog_error (pty, "write: %s", strerror (errno));
        return -1;
    }
    return 0;
}

int flux_pty_sendmsg (struct flux_pty *pty, const flux_msg_t *msg)
{
    struct pty_client *c;
    uint32_t userid;
    const char *type;

    if (!pty || !pty->h || !msg) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_userid (msg, &userid) < 0)
        goto err;
    if (userid != getuid ()) {
        errno = EPERM;
        goto err;
    }
    if (flux_request_unpack (msg, NULL, "{s:s}", "type", &type) < 0) {
        llog_error (pty, "request_unpack: failed to get message type");
        goto err;
    }
    llog_debug (pty, "msg: userid=%u type=%s", userid, type);
    c = pty_client_find_sender (pty, msg);

    if (streq (type, "attach")) {
        /* It is an error for the same client to attach more than once */
        if (c != NULL) {
            errno = EEXIST;
            goto err;
        }
        if (pty_attach (pty, msg) < 0)
            goto err;
        /* pty_attach() starts a streaming response. Skip singleton
         * response below
         */
        check_pty_complete (pty);
        return 0;
    }

    /*  It is an error for the remaining message types to come from
     *   a sender that is not already attached.
     */
    if (c == NULL) {
        errno = ENOENT;
        goto err;
    }
    else if (streq (type, "resize")) {
       if (pty_resize (pty, msg) < 0)
            goto err;
    }
    else if (streq (type, "data")) {
        if (c->write_enabled && pty_write (pty, msg) < 0) {
            llog_error (pty, "pty_write: %s", strerror (errno));
            goto err;
        }
    }
    else if (streq (type, "detach")) {
        if (pty_client_detach (pty, c) < 0)
            goto err;
        if (zlist_size (pty->clients) == 0)
            flux_watcher_stop (pty->fdw);
    }
    else {
        const char *topic;
        flux_msg_get_topic (msg, &topic);
        llog_error (pty, "unhandled message type=%s", type);
        errno = ENOSYS;
        goto err;
    }
    if (flux_respond (pty->h, msg, NULL) < 0)
        llog_error (pty, "flux_respond: %s", flux_strerror (errno));
    return 0;
err:
    if (flux_respond_error (pty->h, msg, errno, NULL) < 0)
        llog_error (pty, "flux_respond_error: %s", flux_strerror (errno));
    return 0;
}

int flux_pty_set_flux (struct flux_pty *pty, flux_t *h)
{
    if (!pty || !h) {
        errno = EINVAL;
        return -1;
    }

    pty->h = h;

    /*  Create pty fd watcher here, but do not start it until the first
     *   client attaches
     */
    pty->fdw = flux_fd_watcher_create (flux_get_reactor (h),
                                       pty->leader,
                                       FLUX_POLLIN,
                                       pty_read,
                                       pty);
    if (!pty->fdw)
        return -1;

    fd_set_nonblocking (pty->leader);

    return 0;
}

int flux_pty_aux_set (struct flux_pty *pty,
                      const char *key,
                      void *val,
                      flux_free_f destroy)
{
    if (!pty) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&pty->aux, key, val, destroy);
}

void * flux_pty_aux_get (struct flux_pty *pty, const char *name)
{
    if (!pty) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (pty->aux, name);

}


/* vi: ts=4 sw=4 expandtab
 */

