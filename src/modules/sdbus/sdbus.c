/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sdbus.c - sd-bus bridge for user-mode systemd
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <systemd/sd-bus.h>
#include <fnmatch.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "message.h"
#include "interface.h"
#include "watcher.h"
#include "subscribe.h"
#include "connect.h"
#include "objpath.h"
#include "sdbus.h"

struct sdbus_ctx {
    bool system_bus; // connect to system bus instead of user bus
    flux_future_t *f_conn; // owns ctx->bus
    sd_bus *bus;
    flux_watcher_t *bus_w;
    flux_msg_handler_t **handlers;
    struct flux_msglist *requests;
    struct flux_msglist *subscribers;
    flux_t *h;

    flux_future_t *f_subscribe;
    uint32_t rank;
};

struct call_info {
    uint64_t cookie;
    char *interface;
    char *member;
};

static void sdbus_recover (struct sdbus_ctx *ctx, const char *reason);

/* Connect retry interval (seconds).
 */
static const double retry_min = 2;
static const double retry_max = 60;

static bool sdbus_debug = false;

static __attribute__ ((format (printf, 2, 3)))
void sdbus_log_debug (flux_t *h, const char *fmt, ...)
{
    if (sdbus_debug) {
        va_list ap;

        va_start (ap, fmt);
        flux_vlog (h, LOG_DEBUG, fmt, ap);
        va_end (ap);
    }
}

static int authorize_request (const flux_msg_t *msg,
                              uint32_t rank,
                              flux_error_t *error)
{
    if (rank != 0 || flux_msg_is_local (msg))
        return 0;
    errprintf (error, "Remote sdbus requests are not allowed on rank 0");
    errno = EPERM;
    return -1;
}

static void bulk_respond_error (flux_t *h,
                                struct flux_msglist *msglist,
                                int errnum,
                                const char *errmsg)
{
    const flux_msg_t *msg;

    while ((msg = flux_msglist_pop (msglist))) {
        if (flux_respond_error (h, msg, errnum, errmsg) < 0) {
            const char *topic = "unknown";
            (void)flux_msg_get_topic (msg, &topic);
            flux_log_error (h, "error responding to %s request", topic);
        }
        flux_msg_decref (msg);
    }
}

static bool match_subscription (const flux_msg_t *msg, sd_bus_message *m)
{
    const char *path_glob = NULL;
    const char *member = NULL;
    const char *interface = NULL;

    (void)flux_request_unpack (msg,
                               NULL,
                               "{s?s s?s s?s}",
                               "path", &path_glob,
                               "interface", &interface,
                               "member", &member);
    if (interface && !streq (interface, sd_bus_message_get_interface (m)))
        return false;
    if (member && !streq (member, sd_bus_message_get_member (m)))
        return false;
    if (path_glob) {
        char *m_path = objpath_decode (sd_bus_message_get_path (m));
        bool match = (m_path && fnmatch (path_glob, m_path, FNM_PATHNAME) == 0);
        free (m_path);
        if (!match)
            return false;
    }
    return true;
}

static bool bulk_respond_match (flux_t *h,
                                struct flux_msglist *msglist,
                                sd_bus_message *m)
{
    json_t *payload = NULL; // decode deferred until match for performance
    const flux_msg_t *msg;
    bool match = false;

    msg = flux_msglist_first (msglist);
    while (msg) {
        if (match_subscription (msg, m)) {
            if (!payload) {
                if (!(payload = interface_signal_tojson (m, NULL)))
                    return false;
            }
            if (flux_respond_pack (h, msg, "O", payload) < 0)
                flux_log_error (h, "error responding to subscribe request");
            else
                match = true;
        }
        msg = flux_msglist_next (msglist);
    }
    json_decref (payload);
    return match;
}

/* Locate a pending sdbus.call request that matches a cookie from a
 * bus method-reply or method-error message.  If infop is non-NULL, assign
 * the message's info struct.
 */
static const flux_msg_t *find_request_by_cookie (struct sdbus_ctx *ctx,
                                                 uint64_t cookie,
                                                 struct call_info **infop)
{
    const flux_msg_t *msg;

    msg = flux_msglist_first (ctx->requests);
    while (msg) {
        struct call_info *info;
        if ((info = flux_msg_aux_get (msg, "info"))
            && cookie == info->cookie) {
            if (infop)
                *infop = info;
            return msg;
        }
        msg = flux_msglist_next (ctx->requests);
    }
    return NULL;
}

/* Log a signal message.
 * If path refers to a systemd unit, make it pretty for the logs.
 */
static void log_msg_signal (flux_t *h,
                            sd_bus_message *m,
                            const char *disposition)
{
    const char *prefix = "/org/freedesktop/systemd1/unit";
    const char *path = sd_bus_message_get_path (m);
    char *s = NULL;

    if (path)
        (void)sd_bus_path_decode (path, prefix, &s);
    sdbus_log_debug (h,
                     "bus %s %s %s %s",
                     disposition,
                     sdmsg_typestr (m),
                     s ? s : path,
                     sd_bus_message_get_member (m));
    free (s);
}

/* Log a method-reply or method-error.
 */
static void log_msg_method_reply (flux_t *h,
                                  sd_bus_message *m,
                                  struct call_info *info)
{
    sdbus_log_debug (h,
                     "bus recv %s cookie=%ju %s",
                     sdmsg_typestr (m),
                     (uintmax_t)info->cookie,
                     info->member);
}

static void sdbus_recv (struct sdbus_ctx *ctx, sd_bus_message *m)
{
    if (sd_bus_message_is_signal (m, NULL, NULL)) {
        const char *path = sd_bus_message_get_path (m);
        const char *iface = sd_bus_message_get_interface (m);
        const char *member = sd_bus_message_get_member (m);

        /* Apparently sd-bus, when it shuts down nicely, gives us a polite
         * note informing us that it can no longer abide our company.
         */
        if (streq (path, "/org/freedesktop/DBus/Local")
            && streq (iface, "org.freedesktop.DBus.Local")
            && streq (member, "Disconnected")) {
            log_msg_signal (ctx->h, m, "recv");
            sdbus_recover (ctx, "received Disconnected signal from bus");
            goto out;
        }
        /* Dispatch handled signals to subscribers here.
         * Log signals with no subscribers as a drop.
         */
        if (bulk_respond_match (ctx->h, ctx->subscribers, m))
            log_msg_signal (ctx->h, m, "recv");
        else
            log_msg_signal (ctx->h, m, "drop");
    }
    else if (sd_bus_message_is_method_call (m, NULL, NULL)) {
        /* Log any method calls (for example requesting introspection) as
         * as "drop".  Flux is purely an sd-bus client and has no methods.
         */
        goto log_drop;
    }
    else if (sd_bus_message_is_method_error (m, NULL)) {
        int errnum = sd_bus_message_get_errno (m);
        const sd_bus_error *error = sd_bus_message_get_error (m);
        uint64_t cookie;
        const flux_msg_t *msg;
        struct call_info *info;

        /* method-error messages that cannot be matched to a pending
         * sdbus.call request are logged as a "drop".  Perhaps the client
         * disconnected without waiting for a reply.
         */
        if (sd_bus_message_get_reply_cookie (m, &cookie) < 0
            || !(msg = find_request_by_cookie (ctx, cookie, &info)))
            goto log_drop;
        /* method-errors that can be matched are logged and dispatched here.
         */
        log_msg_method_reply (ctx->h, m, info);
        if (errnum == 0)
            errnum = EINVAL;
        if (flux_respond_error (ctx->h, msg, errnum, error->message) < 0)
            flux_log_error (ctx->h, "error responding to sdbus.call");
        flux_msglist_delete (ctx->requests); // cursor is on completed message
    }
    else { // method-reply
        uint64_t cookie;
        const flux_msg_t *msg;
        struct call_info *info;
        json_t *rep = NULL;
        flux_error_t error;
        int rc;

        /* method-reply messages that cannot be matched to a pending
         * sdbus.call request are logged as a "drop".  Perhaps the client
         * disconnected without waiting for a reply.
         */
        if (sd_bus_message_get_reply_cookie (m, &cookie) < 0
            || !(msg = find_request_by_cookie (ctx, cookie, &info)))
            goto log_drop;
        /* method-replies that can be matched are logged, translated to json,
         * and dispatched here.  If there's a translation failure, we try to
         * give the requestor a human readable error. This is helpful when
         * developing support for new methods, if nothing else.
         */
        log_msg_method_reply (ctx->h, m, info);
        if ((rep = interface_reply_tojson (m,
                                           info->interface,
                                           info->member,
                                           &error)))
            rc = flux_respond_pack (ctx->h, msg, "O", rep);
        else
            rc = flux_respond_error (ctx->h, msg, EINVAL, error.text);
        if (rc < 0)
            flux_log_error (ctx->h, "error responding to sdbus.call");
        json_decref (rep);
        flux_msglist_delete (ctx->requests); // cursor is on completed message
    }
out:
    return;
log_drop:
    sdbus_log_debug (ctx->h, "bus drop %s", sdmsg_typestr (m));
}

static void call_info_destroy (struct call_info *info)
{
    if (info) {
        int saved_errno = errno;
        free (info->interface);
        free (info->member);
        free (info);
        errno = saved_errno;
    }
}

/* Extract some info from method-call message that will be required when
 * processing method-reply and method-error messages.  The call_info struct
 * will be placed in the aux container of the pending sdbus.call request.
 */
static struct call_info *call_info_create (sd_bus_message *m, uint64_t cookie)
{
    struct call_info *info;
    const char *interface = sd_bus_message_get_interface (m);
    const char *member = sd_bus_message_get_member (m);

    if (!(info = calloc (1, sizeof (*info))))
        return NULL;
    info->cookie = cookie;
    if ((interface && !(info->interface = strdup (interface)))
        || (member && !(info->member = strdup (member))))
        goto error;
    return info;
error:
    call_info_destroy (info);
    return NULL;
}

/* Translate a sdbus.call request to an sd-bus method-call message and send
 * it.  This function is invoked directly by the sdbus.call request handler
 * when the bus is active.  When the bus is inactive, it is called by
 * handle_call_request_backlog() after the bus is reconnected.
 */
static int handle_call_request (struct sdbus_ctx *ctx,
                                const flux_msg_t *msg,
                                flux_error_t *error)
{
    sd_bus_message *m;
    json_t *req;
    int e;
    uint64_t cookie;
    struct call_info *info;

    if (flux_request_unpack (msg, NULL, "o", &req) < 0) {
        errprintf (error, "unable to decode call request");
        return -1;
    }
    if (!(m = interface_request_fromjson (ctx->bus, req, error))) {
        errno = EINVAL;
        goto error;
    }
    if ((e = sd_bus_send (NULL, m, &cookie)) < 0) {
        errno = -e;
        errprintf (error, "error sending sdbus request: %s", strerror (errno));
        goto error;
    }

    sdbus_log_debug (ctx->h,
                     "bus send %s cookie=%ju %s",
                     sdmsg_typestr (m),
                     (uintmax_t)cookie,
                     sd_bus_message_get_member (m));

    if (!(info = call_info_create (m, cookie))
        || flux_msg_aux_set (msg,
                             "info",
                             info,
                             (flux_free_f)call_info_destroy) < 0) {
        call_info_destroy (info);
        errprintf (error, "error saving call request state");
        goto error;
    }
    sd_bus_message_unref (m);
    return 0;
error:
    ERRNO_SAFE_WRAP (sd_bus_message_unref, m);
    return -1;
}

/* Handle an sdbus.call request.
 */
static void call_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (ctx->bus) { // defer request if bus is not yet connected
        if (handle_call_request (ctx, msg, &error) < 0) {
            errmsg = error.text;
            goto error;
        }
    }
    if (flux_msglist_append (ctx->requests, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to call request");
}

/* Handle an sdbus.subscribe request.
 */
static void subscribe_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *s1, *s2, *s3; // not used

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s?s s?s}",
                             "path", &s1,
                             "interface", &s2,
                             "member", &s3) < 0)
        goto error;
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msglist_append (ctx->subscribers, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to sdbus.subscribe request");
}

/* Handle cancellation of an sdbus.subscribe request as described in RFC 6.
 */
static void subscribe_cancel_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    struct sdbus_ctx *ctx = arg;

    if (authorize_request (msg, ctx->rank, NULL) == 0)
        flux_msglist_cancel (h, ctx->subscribers, msg);
}

/* Handle disconnection of a client as described in RFC 6.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct sdbus_ctx *ctx = arg;

    if (authorize_request (msg, ctx->rank, NULL) == 0) {
        (void)flux_msglist_disconnect (ctx->requests, msg);
        (void)flux_msglist_disconnect (ctx->subscribers, msg);
    }
}

/* Handle a request to force bus disconnection and recovery for testing.
 */
static void reconnect_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (authorize_request (msg, ctx->rank, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (!ctx->bus) {
        errmsg = "bus is not connected";
        errno = EINVAL;
        goto error;
    }
    sdbus_recover (ctx, "user requested bus reconnect");
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to sdbus.reconnect request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to sdbus.reconnect request");
}

static int sdbus_configure (struct sdbus_ctx *ctx,
                            const flux_conf_t *conf,
                            flux_error_t *error)
{
    flux_error_t conf_error;
    int debug = 0;

    if (flux_conf_unpack (conf,
                          &conf_error,
                          "{s?{s?b}}",
                          "systemd",
                            "sdbus-debug", &debug) < 0) {
        errprintf (error,
                   "error reading [systemd] config table: %s",
                   conf_error.text);
        return -1;
    }
    sdbus_debug = (debug ? true : false);
    return 0;
}

static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_module_config_request_decode (msg, &conf) < 0) {
        errstr = "Failed to parse config-reload request";
        goto error;
    }
    if (sdbus_configure (ctx, conf, &error) < 0) {
        errstr = error.text;
        goto error_decref;
    }
    if (flux_set_conf_new (h, conf) < 0) {
        errstr = "error updating cached configuration";
        goto error_decref;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error_decref:
    flux_conf_decref (conf);
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
      "call",
      call_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "subscribe",
      subscribe_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "subscribe-cancel",
      subscribe_cancel_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "reconnect",
      reconnect_cb,
      0
    },
    { FLUX_MSGTYPE_REQUEST,
      "config-reload",
      reload_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

/* The bus watcher callback runs sd_bus_process().  Apparently this is an
 * edge triggered notification so we need to handle all events now, which
 * means calling sd_bus_process() in a loop until it returns 0.
 */
static void sdbus_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct sdbus_ctx *ctx = arg;
    int e;

    do {
        sd_bus_message *m = NULL;
        if ((e = sd_bus_process (ctx->bus, &m)) < 0) {
            sdbus_recover (ctx, "error processing sd-bus events");
            return;
        }
        if (m) {
            // sdbus_recv() may call sdbus_recover() which sets ctx->bus = NULL
            sdbus_recv (ctx, m);
            sd_bus_message_unref (m);
        }
    } while (e > 0 && ctx->bus != NULL);
}

/* sdbus.call requests that arrive while the bus connect is in progress
 * are added to ctx->requests without further processing.  Revisit them now
 * and begin processing.  Since recovery fails any pending requests, all
 * requests in ctx->requests are eligible.
 */
static void handle_call_request_backlog (struct sdbus_ctx *ctx)
{
    const flux_msg_t *msg;
    flux_error_t error;
    msg = flux_msglist_first (ctx->requests);
    while (msg)  {
        if (handle_call_request (ctx, msg, &error) < 0) {
            if (flux_respond_error (ctx->h, msg, errno, error.text) < 0)
                flux_log_error (ctx->h, "error responding to call request");
        }
        msg = flux_msglist_next (ctx->requests);
    }
}

/* Bus subscribe completed.  Henceforth, sd-bus signals will be forwarded
 * to subscribers.  Service pending sdbus.call requests.
 * N.B. handle_call_request_backlog is called here rather than when the connect
 * is finalized so that a user may asynchronously subscribe to signals,
 * then initiate an action and expect the subscription to capture all signals
 * triggered by the action.
 */
static void bus_subscribe_continuation (flux_future_t *f, void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_error_t error;

    if (flux_rpc_get (f, NULL) < 0) {
        errprintf (&error, "subscribe error: %s", future_strerror (f, errno));
        goto error;
    }
    handle_call_request_backlog (ctx);
    return;
error:
    sdbus_recover (ctx, error.text);
}

/* Connect completed. Initiate asynchronous bus subscribe.
 */
static void connect_continuation (flux_future_t *f, void *arg)
{
    struct sdbus_ctx *ctx = arg;
    flux_error_t error;

    if (flux_future_get (f, (const void **)&ctx->bus) < 0) {
        errprintf (&error, "sdbus_connect: %s", future_strerror (f, errno));
        goto error;
    }
    if (!(ctx->bus_w = sdbus_watcher_create (flux_get_reactor (ctx->h),
                                             ctx->bus,
                                             sdbus_cb,
                                             ctx))) {
        errprintf (&error, "error creating bus watcher: %s", strerror (errno));
        goto error;
    }
    flux_watcher_start (ctx->bus_w);

    if (!(ctx->f_subscribe = sdbus_subscribe (ctx->h))
        || flux_future_then (ctx->f_subscribe,
                             -1,
                             bus_subscribe_continuation,
                             ctx) < 0) {
        errprintf (&error, "subscribe error: %s", strerror (errno));
        goto error;
    }
    return;
error:
    sdbus_recover (ctx, error.text);
}

static void sdbus_recover (struct sdbus_ctx *ctx, const char *reason)
{

    flux_log (ctx->h, LOG_INFO, "disconnect: %s", reason);

    /* Send any pending requests an error.
     */
    bulk_respond_error (ctx->h, ctx->subscribers, EAGAIN, reason);
    bulk_respond_error (ctx->h, ctx->requests, EAGAIN, reason);

    /* Destroy subscribe future.
     */
    flux_future_destroy (ctx->f_subscribe);
    ctx->f_subscribe = NULL;

    /* Destroy the (now defunct) bus connection and its watcher.
     */
    flux_watcher_destroy (ctx->bus_w);
    ctx->bus_w = NULL;
    flux_future_destroy (ctx->f_conn);
    ctx->f_conn = NULL;
    ctx->bus = NULL;

    /* Begin asynchronous reconnect.
     * Any requests that arrive while this is in progress are deferred.
     * N.B. setting first_time=false ensures a retry_min second delay before
     * the connect attempt.  Some small delay seems to be necessary to avoid
     * libsystemd complaining about unexpected internal states(?) and the
     * occasional segfault.
     */
    if (!(ctx->f_conn = sdbus_connect (ctx->h,
                                       false,
                                       retry_min,
                                       retry_max,
                                       ctx->system_bus))
        || flux_future_then (ctx->f_conn, -1, connect_continuation, ctx) < 0) {
        flux_log_error (ctx->h, "error starting bus connect");
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
    }
}

static int parse_module_args (struct sdbus_ctx *ctx,
                              int argc,
                              char **argv,
                              flux_error_t *error)
{
    for (int i = 0; i < argc; i++) {
        if (streq (argv[i], "system"))
            ctx->system_bus = true;
        else {
            errprintf (error, "unknown module option: %s", argv[i]);
            return -1;
        }
    }
    return 0;
}

void sdbus_ctx_destroy (struct sdbus_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        const char *errmsg = "module is unloading";
        if (ctx->subscribers) {
            bulk_respond_error (ctx->h, ctx->subscribers, ENOSYS, errmsg);
            flux_msglist_destroy (ctx->subscribers);
        }
        if (ctx->requests) {
            bulk_respond_error (ctx->h, ctx->requests, ENOSYS, errmsg);
            flux_msglist_destroy (ctx->requests);
        }
        flux_msg_handler_delvec (ctx->handlers);
        flux_watcher_destroy (ctx->bus_w);
        flux_future_destroy (ctx->f_subscribe);
        if (ctx->bus) {
            sd_bus_flush (ctx->bus);
            sd_bus_close (ctx->bus);
        }
        flux_future_destroy (ctx->f_conn); // destroys ctx->bus
        free (ctx);
        errno = saved_errno;
    }
}

struct sdbus_ctx *sdbus_ctx_create (flux_t *h,
                                    int argc,
                                    char **argv,
                                    flux_error_t *error)
{
    struct sdbus_ctx *ctx;
    const char *name = flux_aux_get (h, "flux::name");

    if (!(ctx = calloc (1, sizeof (*ctx))))
        goto error_create;
    if (parse_module_args (ctx, argc, argv, error) < 0)
        goto error;
    if (sdbus_configure (ctx, flux_get_conf (h), error) < 0)
        goto error;
    if (!(ctx->f_conn = sdbus_connect (h,
                                       true,
                                       retry_min,
                                       retry_max,
                                       ctx->system_bus))
        || flux_future_then (ctx->f_conn, -1, connect_continuation, ctx) < 0
        || flux_msg_handler_addvec_ex (h, name, htab, ctx, &ctx->handlers) < 0
        || !(ctx->requests = flux_msglist_create ())
        || !(ctx->subscribers = flux_msglist_create ())
        || flux_get_rank (h, &ctx->rank) < 0)
        goto error_create;
    ctx->h = h;
    return ctx;
error_create:
    errprintf (error, "error creating sdbus context: %s", strerror (errno));
error:
    sdbus_ctx_destroy (ctx);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
