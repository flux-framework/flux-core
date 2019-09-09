/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* guest_watch.c - handle job-info.guest-eventlog-watch &
 * job-info.guest-eventlog-watch-cancel for job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"
#include "src/common/libeventlog/eventlog.h"

#include "info.h"
#include "watch.h"
#include "allow.h"

/* The callback for job-info.guest-eventlog-watch handles all
 * of the tricky / racy things related to reading an eventlog from the
 * guest namespace.  Effectively it is a state machine, checking the
 * main job eventlog (via job-info.lookup) to determine what state the
 * guest eventlog is in.  Based on the results, calls are made to
 * job-info.eventlog-watch to wait or determine how to read from the
 * guest eventlog.
 *
 * Here is an overview of what the code below does:
 *
 * 1) Check the main eventlog, both for access & to see how far the
 *    job is along (get_main_eventlog()).
 *
 * 2) If the guest namespace is already copied into the main namespace
 *    (event "release" and "final=true"), we watch the main eventlog
 *    (main_namespace_watch()).  This is "easy" case and is not so
 *    different from a typical call to 'job-info.eventlog-watch'.
 *
 * 3) If the guest namespace is still active (event "start" in the
 *    main eventlog, but not "release"), we need to watch the eventlog
 *    directly from the guest namespce instead of the main KVS
 *    namespace (guest_namespace_watch()).
 *
 * 3A) There is a race where the guest namespace has been removed
 *     after part #1 above, but before we start reading it via a call
 *     in #3.  Detect this case and convert to watching the main
 *     namespace (#2).
 *
 * 4) If the namespace has not yet been created (event "start" has not
 *    ocurred), must wait for the guest namespace to be created
 *    (wait_guest_namespace()), then eventually follow the path of
 *    watching events in the guest namespace (#3).
 */

struct guest_watch_ctx {
    struct info_ctx *ctx;
    const flux_msg_t *msg;
    uint32_t msg_rolemask;
    uint32_t msg_userid;
    flux_jobid_t id;
    char *path;
    bool cancel;

    /* transition possibilities
     *
     * INIT -> GET_MAIN_EVENTLOG - this is when we check the main
     * eventlog to see what state the job is in.
     *
     * GET_MAIN_EVENTLOG -> WAIT_GUEST_NAMESPACE - guest namespace
     * not yet created, wait for its creation
     *
     * GET_MAIN_EVENTLOG -> GUEST_NAMESPACE_WATCH - guest namespace
     * created, so we should watch it
     *
     * GET_MAIN_EVENTLOG -> MAIN_NAMESPACE_WATCH - guest namespace
     * moved to main namespace, so watch in main namespace
     *
     * WAIT_GUEST_NAMESPACE -> GUEST_NAMESPACE_WATCH - guest namespace
     * created, so we should watch it
     *
     * GUEST_NAMESPACE_WATCH -> MAIN_NAMESPACE_WATCH - under a racy
     * situation, guest namespace could be removed before we began to
     * read from it.  If so, transition to watch in main namespace
     */
    enum {
        GUEST_WATCH_STATE_INIT = 1,
        GUEST_WATCH_STATE_GET_MAIN_EVENTLOG = 2,
        GUEST_WATCH_STATE_WAIT_GUEST_NAMESPACE = 3,
        GUEST_WATCH_STATE_GUEST_NAMESPACE_WATCH = 4,
        GUEST_WATCH_STATE_MAIN_NAMESPACE_WATCH = 5,
    } state;

    flux_future_t *get_main_eventlog_f;
    flux_future_t *wait_guest_namespace_f;
    flux_future_t *guest_namespace_watch_f;
    flux_future_t *main_namespace_watch_f;

    /* flags indicating what was found in main eventlog */
    bool guest_started;
    bool guest_released;

    /* indicates if events have been read from the guest namespace
     * eventlog */
    bool guest_namespace_events;
    /* indicates if the guest namespace has been removed */
    bool guest_namespace_removed;
};

static int get_main_eventlog (struct guest_watch_ctx *gw);
static void get_main_eventlog_continuation (flux_future_t *f, void *arg);
static int wait_guest_namespace (struct guest_watch_ctx *gw);
static void wait_guest_namespace_continuation (flux_future_t *f, void *arg);
static int guest_namespace_watch (struct guest_watch_ctx *gw);
static void guest_namespace_watch_continuation (flux_future_t *f, void *arg);
static int main_namespace_watch (struct guest_watch_ctx *gw);
static void main_namespace_watch_continuation (flux_future_t *f, void *arg);

static void guest_watch_ctx_destroy (void *data)
{
    if (data) {
        struct guest_watch_ctx *gw = data;
        flux_msg_decref (gw->msg);
        free (gw->path);
        flux_future_destroy (gw->get_main_eventlog_f);
        flux_future_destroy (gw->wait_guest_namespace_f);
        flux_future_destroy (gw->guest_namespace_watch_f);
        flux_future_destroy (gw->main_namespace_watch_f);
        free (gw);
    }
}

static struct guest_watch_ctx *guest_watch_ctx_create (struct info_ctx *ctx,
                                                       const flux_msg_t *msg,
                                                       flux_jobid_t id,
                                                       const char *path)
{
    struct guest_watch_ctx *gw = calloc (1, sizeof (*gw));
    int saved_errno;

    if (!gw)
        return NULL;

    gw->ctx = ctx;
    gw->id = id;
    if (!(gw->path = strdup (path))) {
        errno = ENOMEM;
        goto error;
    }
    gw->state = GUEST_WATCH_STATE_INIT;

    gw->msg = flux_msg_incref (msg);

    if (flux_msg_get_rolemask (msg, &gw->msg_rolemask) < 0) {
        flux_log_error (ctx->h, "%s: flux_msg_get_rolemask", __FUNCTION__);
        goto error;
    }
    if (flux_msg_get_userid (msg, &gw->msg_userid) < 0) {
        flux_log_error (ctx->h, "%s: flux_msg_get_userid", __FUNCTION__);
        goto error;
    }

    return gw;

error:
    saved_errno = errno;
    guest_watch_ctx_destroy (gw);
    errno = saved_errno;
    return NULL;
}

/* we want to copy rolemask, userid, etc. from the original
 * message when we redirect to other job-info targets.
 */
static flux_msg_t *guest_msg_pack (struct guest_watch_ctx *gw,
                                   const char *topic,
                                   const char *fmt,
                                   ...)
{
    flux_msg_t *newmsg = NULL;
    json_t *payload = NULL;
    char *payloadstr = NULL;
    flux_msg_t *rv = NULL;
    int save_errno;
    va_list ap;

    va_start (ap, fmt);

    if (!(newmsg = flux_request_encode (topic, NULL)))
        goto error;
    if (flux_msg_set_rolemask (newmsg, gw->msg_rolemask) < 0)
        goto error;
    if (flux_msg_set_userid (newmsg, gw->msg_userid) < 0)
        goto error;
    if (!(payload = json_vpack_ex (NULL, 0, fmt, ap)))
        goto error;
    if (!(payloadstr = json_dumps (payload, JSON_COMPACT))) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_set_string (newmsg, payloadstr) < 0)
        goto error;

    rv = newmsg;
error:
    save_errno = errno;
    if (!rv)
        flux_msg_destroy (newmsg);
    json_decref (payload);
    free (payloadstr);
    va_end (ap);
    errno = save_errno;
    return rv;
}

static int send_cancel (struct guest_watch_ctx *gw, flux_future_t *f)
{
    if (!gw->cancel) {
        flux_future_t *f2;
        int matchtag;

        if (!f) {
            if (gw->state == GUEST_WATCH_STATE_WAIT_GUEST_NAMESPACE)
                f = gw->wait_guest_namespace_f;
            else if (gw->state == GUEST_WATCH_STATE_GUEST_NAMESPACE_WATCH) {
                /* if guest namespace removed, nothing to cancel.  So
                 * send back ENODATA to caller. */
                if (gw->guest_namespace_removed) {
                    gw->cancel = true;
                    if (flux_respond_error (gw->ctx->h,
                                            gw->msg,
                                            ENODATA,
                                            NULL) < 0)
                        flux_log_error (gw->ctx->h, "%s: flux_respond_error",
                                        __FUNCTION__);
                    return 0;
                }
                f = gw->guest_namespace_watch_f;
            }
            else if (gw->state == GUEST_WATCH_STATE_MAIN_NAMESPACE_WATCH)
                f = gw->main_namespace_watch_f;
            else {
                /* nothing to cancel */
                gw->cancel = true;
                return 0;
            }
        }

        matchtag = (int)flux_rpc_get_matchtag (f);

        if (!(f2 = flux_rpc_pack (gw->ctx->h,
                                 "job-info.eventlog-watch-cancel",
                                 FLUX_NODEID_ANY,
                                 FLUX_RPC_NORESPONSE,
                                 "{s:i}",
                                  "matchtag", matchtag))) {
            flux_log_error (gw->ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
            return -1;
        }
        flux_future_destroy (f2);
        gw->cancel = true;
    }
    return 0;
}

static int get_main_eventlog (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.lookup";
    flux_msg_t *msg = NULL;
    int save_errno, rv = -1;

    if (!(msg = guest_msg_pack (gw,
                                topic,
                                "{s:I s:[s] s:i}",
                                "id", gw->id,
                                "keys", "eventlog",
                                "flags", 0)))
        goto error;

    if (!(gw->get_main_eventlog_f = flux_rpc_message (gw->ctx->h,
                                                      msg,
                                                      FLUX_NODEID_ANY,
                                                      0))) {
        flux_log_error (gw->ctx->h, "%s: flux_rpc_message", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (gw->get_main_eventlog_f,
                          -1,
                          get_main_eventlog_continuation,
                          gw) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (gw->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    gw->state = GUEST_WATCH_STATE_GET_MAIN_EVENTLOG;
    rv = 0;
error:
    save_errno = errno;
    flux_msg_destroy (msg);
    errno = save_errno;
    return rv;
}

/* if we see the event "start", we know the guest namespace has
 * definitely been created, but we can't guarantee it before that.
 *
 * if we see the event "release" with "final=true", we know the guest
 * namespace has definitely been removed / moved into the main KVS.
 */
static int check_guest_namespace_status (struct guest_watch_ctx *gw,
                                         const char *s)
{
    json_t *a = NULL;
    size_t index;
    json_t *event;
    int rv = -1;

    if (!(a = eventlog_decode (s)))
        goto error;

    json_array_foreach (a, index, event) {
        const char *name;
        json_t *context = NULL;
        if (eventlog_entry_parse (event, NULL, &name, &context) < 0)
            goto error;
        if (!strcmp (name, "start"))
            gw->guest_started = true;
        if (!strcmp (name, "release")) {
            void *iter = json_object_iter (context);
            while (iter && !gw->guest_released) {
                const char *key = json_object_iter_key (iter);
                if (!strcmp (key, "final")) {
                    json_t *value = json_object_iter_value (iter);
                    if (json_is_boolean (value) && json_is_true (value))
                        gw->guest_released = true;
                }
                iter = json_object_iter_next (context, iter);
            }
        }
    }

    rv = 0;
error:
    json_decref (a);
    return rv;
}

static void get_main_eventlog_continuation (flux_future_t *f, void *arg)
{
    struct guest_watch_ctx *gw = arg;
    struct info_ctx *ctx = gw->ctx;
    const char *s;

    if (flux_rpc_get_unpack (f, "{s:s}", "eventlog", &s) < 0) {
        if (errno != ENOENT && errno != EPERM)
            flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    if (gw->cancel) {
        if (flux_respond_error (ctx->h, gw->msg, ENODATA, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }

    if (check_guest_namespace_status (gw, s) < 0)
        goto error;

    if (gw->guest_released) {
        /* guest namespace copied to main KVS, just watch it like normal */
        if (main_namespace_watch (gw) < 0)
            goto error;
    }
    else if (gw->guest_started) {
        /* guest namespace created, watch it and not the main KVS
         * namespace */
        if (guest_namespace_watch (gw) < 0)
            goto error;
    }
    else {
        /* wait eventlog for start */
        if (wait_guest_namespace (gw) < 0)
            goto error;
    }

    return;

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
done:
    /* flux future destroyed in guest_watch_ctx_destroy, which is
     * called via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

static int wait_guest_namespace (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    flux_msg_t *msg = NULL;
    int save_errno, rv = -1;

    if (!(msg = guest_msg_pack (gw,
                                topic,
                                "{s:I s:s}",
                                "id", gw->id,
                                "path", "eventlog")))
        goto error;

    if (!(gw->wait_guest_namespace_f = flux_rpc_message (gw->ctx->h,
                                                         msg,
                                                         FLUX_NODEID_ANY,
                                                         rpc_flags))) {
        flux_log_error (gw->ctx->h, "%s: flux_rpc_message", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (gw->wait_guest_namespace_f,
                          -1,
                          wait_guest_namespace_continuation,
                          gw) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (gw->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    gw->state = GUEST_WATCH_STATE_WAIT_GUEST_NAMESPACE;
    rv = 0;
error:
    save_errno = errno;
    flux_msg_destroy (msg);
    errno = save_errno;
    return rv;
}

static int check_guest_namespace_created (struct guest_watch_ctx *gw,
                                          const char *event)
{
    json_t *o = NULL;
    const char *name;
    int save_errno, rv = -1;

    if (!(o = eventlog_entry_decode (event))) {
        flux_log_error (gw->ctx->h, "%s: eventlog_entry_decode", __FUNCTION__);
        goto error;
    }

    if (eventlog_entry_parse (o, NULL, &name, NULL) < 0) {
        flux_log_error (gw->ctx->h, "%s: eventlog_entry_decode", __FUNCTION__);
        goto error;
    }

    if (!strcmp (name, "start"))
        gw->guest_started = true;

    rv = 0;
error:
    save_errno = errno;
    json_decref (o);
    errno = save_errno;
    return rv;
}

static void wait_guest_namespace_continuation (flux_future_t *f, void *arg)
{
    struct guest_watch_ctx *gw = arg;
    struct info_ctx *ctx = gw->ctx;
    const char *event;

    if (flux_rpc_get (f, NULL) < 0) {
        if (errno == ENODATA) {
            /* either user canceled this watch, or we did.  If we did,
             * its because the guest namespace is now created and now
             * we're going to watch it */
            if (gw->guest_started) {
                if (guest_namespace_watch (gw) < 0)
                    goto error;
                return;
            }
            goto error;
        }
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_rpc_get", __FUNCTION__);
        goto error;
    }

    if (gw->cancel) {
        errno = ENODATA;
        goto error;
    }

    if (flux_job_event_watch_get (f, &event) < 0) {
        flux_log_error (ctx->h, "%s: flux_job_event_watch_get", __FUNCTION__);
        goto error_cancel;
    }

    if (check_guest_namespace_created (gw, event) < 0)
        goto error_cancel;

    if (gw->guest_started) {
        int matchtag = (int)flux_rpc_get_matchtag (gw->wait_guest_namespace_f);
        flux_future_t *f2;

        /* cancel this watcher, and once its canceled, watch the guest
         * namespace.  Don't call send_cancel(), this is not an error
         * or "full" cancel */
        if (!(f2 = flux_rpc_pack (gw->ctx->h,
                                  "job-info.eventlog-watch-cancel",
                                  FLUX_NODEID_ANY,
                                  FLUX_RPC_NORESPONSE,
                                  "{s:i}",
                                  "matchtag", matchtag))) {
            flux_log_error (gw->ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
            goto error;
        }
        flux_future_destroy (f2);
    }

    flux_future_reset (f);
    return;

error_cancel:
    /* If we haven't sent a cancellation yet, must do so so that
     * the future's matchtag will eventually be freed */
    if (!gw->cancel) {
        int save_errno = errno;
        (void) send_cancel (gw, gw->wait_guest_namespace_f);
        errno = save_errno;
    }

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

    /* flux future destroyed in guest_watch_ctx_destroy, which is
     * called via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

static int guest_namespace_watch (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    flux_msg_t *msg = NULL;
    int save_errno;
    int rv = -1;

    if (!(msg = guest_msg_pack (gw,
                                topic,
                                "{s:I s:b s:s}",
                                "id", gw->id,
                                "guest", true,
                                "path", gw->path)))
        goto error;

    if (!(gw->guest_namespace_watch_f = flux_rpc_message (gw->ctx->h,
                                                          msg,
                                                          FLUX_NODEID_ANY,
                                                          rpc_flags))) {
        flux_log_error (gw->ctx->h, "%s: flux_rpc_message", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (gw->guest_namespace_watch_f,
                          -1,
                          guest_namespace_watch_continuation,
                          gw) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (gw->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    gw->state = GUEST_WATCH_STATE_GUEST_NAMESPACE_WATCH;
    rv = 0;
error:
    save_errno = errno;
    flux_msg_destroy (msg);
    errno = save_errno;
    return rv;
}

static void guest_namespace_watch_continuation (flux_future_t *f, void *arg)
{
    struct guest_watch_ctx *gw = arg;
    struct info_ctx *ctx = gw->ctx;
    const char *s;

    if (flux_rpc_get (f, &s) < 0) {
        if (errno == ENOTSUP) {
            /* Guest namespace has been removed.  If we have read no
             * events in the guest eventlog, assume job was moved into
             * the main namespace before we began watching in the
             * guest namespace.
             *
             * Note that it is possible the guest eventlog is simply
             * empty / had no events in it.  There's no way to know
             * for certain if it is this case or a race.  This is an
             * unfortunate behavior difference.  Issue #2356.
             */
            gw->guest_namespace_removed = true;
            if (!gw->guest_namespace_events) {
                if (main_namespace_watch (gw) < 0)
                    goto error;
            }
            else if (gw->cancel) {
                /* Racy scenario - user attempted a cancel right as
                 * ENOTSUP being sent.  Caller won't get a ENODATA
                 * response b/c the original watcher is now dead.
                 */
                errno = ENODATA;
                goto error;
            }
            return;
        }
        else {
            /* We assume ENODATA always comes from a user cancellation,
             * or similar error.  There is no circumstance where would
             * desire to ENODATA this stream.
             */
            if (errno != ENOENT && errno != ENODATA)
                flux_log_error (ctx->h, "%s: flux_rpc_get", __FUNCTION__);
            goto error;
        }
    }

    if (gw->cancel) {
        errno = ENODATA;
        goto error;
    }

    if (flux_respond (ctx->h, gw->msg, s) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
        goto error_cancel;
    }

    gw->guest_namespace_events = true;
    flux_future_reset (f);
    return;

error_cancel:
    /* If we haven't sent a cancellation yet, must do so so that
     * the future's matchtag will eventually be freed */
    if (!gw->cancel) {
        int save_errno = errno;
        (void) send_cancel (gw, gw->guest_namespace_watch_f);
        errno = save_errno;
    }

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

    /* flux future destroyed in guest_watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

static int main_namespace_watch (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    flux_msg_t *msg = NULL;
    int save_errno;
    int rv = -1;
    char path[64];
    int tmp;

    /* must prefix "guest." back to path */
    tmp = snprintf (path, sizeof (path), "guest.%s", gw->path);
    if (tmp >= sizeof (path)) {
        errno = EOVERFLOW;
        goto error;
    }

    if (!(msg = guest_msg_pack (gw,
                                topic,
                                "{s:I s:b s:s}",
                                "id", gw->id,
                                "guest", false,
                                "path", path)))
        goto error;

    if (!(gw->main_namespace_watch_f = flux_rpc_message (gw->ctx->h,
                                                         msg,
                                                         FLUX_NODEID_ANY,
                                                         rpc_flags))) {
        flux_log_error (gw->ctx->h, "%s: flux_rpc_message", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (gw->main_namespace_watch_f,
                          -1,
                          main_namespace_watch_continuation,
                          gw) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (gw->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    gw->state = GUEST_WATCH_STATE_MAIN_NAMESPACE_WATCH;
    rv = 0;
error:
    save_errno = errno;
    flux_msg_destroy (msg);
    errno = save_errno;
    return rv;
}

static void main_namespace_watch_continuation (flux_future_t *f, void *arg)
{
    struct guest_watch_ctx *gw = arg;
    struct info_ctx *ctx = gw->ctx;
    const char *s;

    if (flux_rpc_get (f, &s) < 0) {
        if (errno != ENOENT && errno != ENODATA)
            flux_log_error (ctx->h, "%s: flux_rpc_get", __FUNCTION__);
        goto error;
    }

    if (flux_respond (ctx->h, gw->msg, s) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
        goto error_cancel;
    }

    flux_future_reset (f);
    return;

error_cancel:
    /* If we haven't sent a cancellation yet, must do so so that
     * the future's matchtag will eventually be freed */
    if (!gw->cancel) {
        int save_errno = errno;
        (void) send_cancel (gw, gw->main_namespace_watch_f);
        errno = save_errno;
    }

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

    /* flux future destroyed in guest_watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

void guest_watch_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct guest_watch_ctx *gw = NULL;
    flux_jobid_t id;
    const char *path = NULL;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:s}",
                             "id", &id,
                             "path", &path) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "guest-eventlog-watch request rejected without streaming "
                 "RPC flag";
        goto error;
    }

    if (!(gw = guest_watch_ctx_create (ctx, msg, id, path)))
        goto error;

    if (get_main_eventlog (gw) < 0)
        goto error;

    if (zlist_append (ctx->guest_watchers, gw) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->guest_watchers, gw, guest_watch_ctx_destroy, true);
    gw = NULL;

    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    guest_watch_ctx_destroy (gw);
}

/* Cancel guest_watch 'gw' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 */
static void guest_watch_cancel (struct info_ctx *ctx,
                                struct guest_watch_ctx *gw,
                                const char *sender, uint32_t matchtag)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
        && (flux_msg_get_matchtag (gw->msg, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (gw->msg, &s) < 0)
        return;
    if (!strcmp (sender, s))
        send_cancel (gw, NULL);
    free (s);
}

void guest_watchers_cancel (struct info_ctx *ctx,
                            const char *sender, uint32_t matchtag)
{
    struct guest_watch_ctx *gw;

    gw = zlist_first (ctx->guest_watchers);
    while (gw) {
        guest_watch_cancel (ctx, gw, sender, matchtag);
        gw = zlist_next (ctx->guest_watchers);
    }
}

void guest_watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                            const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    uint32_t matchtag;
    char *sender;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    guest_watchers_cancel (ctx, sender, matchtag);
    free (sender);
}

void guest_watch_cleanup (struct info_ctx *ctx)
{
    struct guest_watch_ctx *gw;

    while ((gw = zlist_pop (ctx->guest_watchers))) {
        send_cancel (gw, NULL);

        if (flux_respond_error (ctx->h, gw->msg, ENOSYS, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error",
                            __FUNCTION__);
        guest_watch_ctx_destroy (gw);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
