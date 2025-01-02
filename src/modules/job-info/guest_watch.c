/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* guest_watch.c - handle guest eventlog logic for
 * job-info.eventlog-watch & job-info.eventlog-watch-cancel for
 * job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <limits.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

#include "job-info.h"
#include "util.h"
#include "watch.h"

/* This code (entrypoint guest_watch()) handles all
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
 *    (event "release" and "final=true"), we watch the eventlog in the
 *    main namespace (main_namespace_watch()).  This is the "easy" case
 *    and is not so different from a typical call to
 *    'job-info.eventlog-watch'.
 *
 * 3) If the guest namespace is still active (event "start" in the
 *    main eventlog, but not "release"), we need to watch the eventlog
 *    directly from the guest namespace instead of the primary KVS
 *    namespace (guest_namespace_watch()).  After the guest namespace
 *    is removed, we fallthrough to the primary KVS namespace.  This
 *    fallthrough to the primary KVS namespace corrects two potential
 *    races.
 *
 *    - There is a very small racy scenario where data could be lost
 *    during a kvs-watch and namespace removal.  See issue #2386 for
 *    details.
 *
 *     - The guest namespace has been removed after part #1 above, but
 *     before we start reading it via a call in #3.
 *
 * 4) If the namespace has not yet been created (event "start" has not
 *    occurred), must wait for the guest namespace to be created
 *    (wait_guest_namespace()), then eventually follow the path of
 *    watching events in the guest namespace (#3).
 */

struct guest_watch_ctx {
    struct info_ctx *ctx;
    const flux_msg_t *msg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    char *path;
    int flags;
    bool eventlog_watch_canceled;
    bool cancel;                /* cancel or disconnect */

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

    /* data from guest namespace */
    int guest_offset;
    /* data from main namespace */
    int main_offset;
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
        int save_errno = errno;
        flux_msg_decref (gw->msg);
        free (gw->path);
        flux_future_destroy (gw->get_main_eventlog_f);
        flux_future_destroy (gw->wait_guest_namespace_f);
        flux_future_destroy (gw->guest_namespace_watch_f);
        flux_future_destroy (gw->main_namespace_watch_f);
        free (gw);
        errno = save_errno;
    }
}

static struct guest_watch_ctx *guest_watch_ctx_create (struct info_ctx *ctx,
                                                       const flux_msg_t *msg,
                                                       flux_jobid_t id,
                                                       const char *path,
                                                       int flags)
{
    struct guest_watch_ctx *gw = calloc (1, sizeof (*gw));

    if (!gw)
        return NULL;

    gw->ctx = ctx;
    gw->id = id;
    if (!(gw->path = strdup (path))) {
        errno = ENOMEM;
        goto error;
    }
    gw->flags = flags;
    gw->state = GUEST_WATCH_STATE_INIT;

    gw->msg = flux_msg_incref (msg);

    if (flux_msg_get_cred (msg, &gw->cred) < 0) {
        flux_log_error (ctx->h, "%s: flux_msg_get_cred", __FUNCTION__);
        goto error;
    }

    return gw;

error:
    guest_watch_ctx_destroy (gw);
    return NULL;
}

static int send_eventlog_watch_cancel (struct guest_watch_ctx *gw,
                                       flux_future_t *f,
                                       bool cancel)
{
    if (!gw->eventlog_watch_canceled) {
        flux_future_t *f2;
        int matchtag;

        gw->cancel = cancel;

        if (!f) {
            if (gw->state == GUEST_WATCH_STATE_WAIT_GUEST_NAMESPACE)
                f = gw->wait_guest_namespace_f;
            else if (gw->state == GUEST_WATCH_STATE_GUEST_NAMESPACE_WATCH)
                f = gw->guest_namespace_watch_f;
            else if (gw->state == GUEST_WATCH_STATE_MAIN_NAMESPACE_WATCH)
                f = gw->main_namespace_watch_f;
            else {
                /* gw->state == GUEST_WATCH_STATE_INIT, eventlog-watch
                 * never started so sort of "auto-canceled" */
                gw->eventlog_watch_canceled = true;
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
        gw->eventlog_watch_canceled = true;
    }
    return 0;
}

static int get_main_eventlog (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.lookup";
    flux_msg_t *msg = NULL;
    int save_errno, rv = -1;

    if (!(msg = cred_msg_pack (topic,
                               gw->cred,
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
        if (streq (name, "start"))
            gw->guest_started = true;
        if (streq (name, "release")) {
            void *iter = json_object_iter (context);
            while (iter && !gw->guest_released) {
                const char *key = json_object_iter_key (iter);
                if (streq (key, "final")) {
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

    if (gw->eventlog_watch_canceled) {
        if (gw->cancel) {
            if (flux_respond_error (ctx->h, gw->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        }
        goto done;
    }

    /* N.B. Check for whether requester should be allowed to read this
     * eventlog could be done here (eventlog_allow ()), however since
     * it will be done in the primary watch code anyways, we let the
     * check fallthrough to be done there.
     */

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

    if (!(msg = cred_msg_pack (topic,
                               gw->cred,
                               "{s:I s:s s:i}",
                               "id", gw->id,
                               "path", "eventlog",
                               "flags", 0)))
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

    if (streq (name, "start"))
        gw->guest_started = true;

    /* Do not need to check for "clean", if "start" never occurs, will
     * eventually get ENODATA */

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
            /* guest_started indicates we canceled this watch,the
             * guest namespace is now created, and we're now going to
             * watch it.  If the guest namespace has not started,
             * either the user canceled or the job never started and
             * we got ENODATA from the eventlog watcher reaching the
             * end of the eventlog. */
            if (gw->guest_started) {
                /* check for racy cancel - user canceled while this
                 * error was in transit */
                if (gw->eventlog_watch_canceled) {
                    errno = ENODATA;
                    if (gw->cancel)
                        goto error;
                    goto cleanup;
                }
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

    if (gw->eventlog_watch_canceled) {
        errno = ENODATA;
        if (gw->cancel)
            goto error;
        goto cleanup;
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
         * namespace.  Don't call send_eventlog_watch_cancel(), this
         * is not an error or "full" cancel */
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
    if (!gw->eventlog_watch_canceled) {
        int save_errno = errno;
        (void) send_eventlog_watch_cancel (gw,
                                           gw->wait_guest_namespace_f,
                                           false);
        errno = save_errno;
    }

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
cleanup:
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

    if (!(msg = cred_msg_pack (topic,
                               gw->cred,
                               "{s:I s:b s:s s:i}",
                               "id", gw->id,
                               "guest", true,
                               "path", gw->path,
                               "flags", gw->flags)))
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
    const char *event;

    if (flux_job_event_watch_get (f, &event) < 0) {
        if (errno == ENOTSUP) {
            /* Guest namespace has been removed and eventlog has been
             * moved to primary KVS namespace.  Fallthrough to primary
             * KVS namespace.
             *
             * The fallthrough to the primary KVS namespace fixes two
             * racy scenarios:
             *
             * - the namespace has been removed prior to our original
             *   request to read from it.
             * - racy scenario where data from a kvs-watch is missed
             *   b/c of the namespace removal (see issue #2386 for
             *   details).  The tracking of data read/sent via the
             *   offset variable will determine if we have more data
             *   to send from the primary KVS namespace.
             */
            /* check for racy cancel - user canceled while this
             * error was in transit */
            if (gw->eventlog_watch_canceled) {
                errno = ENODATA;
                if (gw->cancel)
                    goto error;
                goto cleanup;
            }
            if (main_namespace_watch (gw) < 0)
                goto error;
            return;
        }
        else {
            /* Generally speaking we assume ENODATA always comes from
             * a user cancellation, or similar error.  There is no
             * circumstance where would desire to ENODATA this stream.
             */
            if (errno != ENOENT && errno != ENODATA)
                flux_log_error (ctx->h, "%s: flux_rpc_get", __FUNCTION__);
            goto error;
        }
    }

    if (gw->eventlog_watch_canceled) {
        if (gw->cancel) {
            errno = ENODATA;
            goto error;
        }
        goto cleanup;
    }

    if (flux_respond_pack (ctx->h, gw->msg, "{s:s}", "event", event) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond_pack",
                        __FUNCTION__);

        /* If we haven't sent a cancellation yet, must do so so that
         * the future's matchtag will eventually be freed */
        if (!gw->eventlog_watch_canceled)
            (void) send_eventlog_watch_cancel (gw,
                                               gw->guest_namespace_watch_f,
                                               false);
        goto cleanup;
    }

    gw->guest_offset += strlen (event);
    flux_future_reset (f);
    return;

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
cleanup:
    /* flux future destroyed in guest_watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

/* must prefix "guest." back to path when watching in main KVS
 * namespace */
static int full_guest_path (struct guest_watch_ctx *gw,
                            char *path,
                            size_t path_len)
{
    int tmp;

    tmp = snprintf (path, path_len, "guest.%s", gw->path);
    if (tmp >= path_len) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

static int main_namespace_watch (struct guest_watch_ctx *gw)
{
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    flux_msg_t *msg = NULL;
    int save_errno;
    int flags = gw->flags;
    int rv = -1;
    char path[PATH_MAX];

    if (full_guest_path (gw, path, PATH_MAX) < 0)
        goto error;

    /* the job has completed, so "waitcreate" has no meaning
     * anymore, clear the flag
     */
    if (flags & FLUX_JOB_EVENT_WATCH_WAITCREATE)
        flags &= ~FLUX_JOB_EVENT_WATCH_WAITCREATE;

    if (!(msg = cred_msg_pack (topic,
                               gw->cred,
                               "{s:I s:b s:s s:i}",
                               "id", gw->id,
                               "guest_in_main", true,
                               "path", path,
                               "flags", flags)))
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
    const char *event;

    if (flux_job_event_watch_get (f, &event) < 0) {
        if (errno != ENOENT && errno != ENODATA)
            flux_log_error (ctx->h,
                            "%s: flux_job_event_watch_get",
                            __FUNCTION__);
        goto error;
    }

    if (gw->eventlog_watch_canceled) {
        if (gw->cancel) {
            errno = ENODATA;
            goto error;
        }
        goto cleanup;
    }

    gw->main_offset += strlen (event);

    if (gw->main_offset > gw->guest_offset) {
        if (flux_respond_pack (ctx->h, gw->msg, "{s:s}", "event", event) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond_pack",
                            __FUNCTION__);

            /* If we haven't sent a cancellation yet, must do so so that
             * the future's matchtag will eventually be freed */
            if (!gw->eventlog_watch_canceled)
                (void) send_eventlog_watch_cancel (gw,
                                                   gw->main_namespace_watch_f,
                                                   false);
            goto cleanup;
        }
    }

    flux_future_reset (f);
    return;

error:
    if (flux_respond_error (ctx->h, gw->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
cleanup:
    /* flux future destroyed in guest_watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->guest_watchers, gw);
}

int guest_watch (struct info_ctx *ctx,
                 const flux_msg_t *msg,
                 flux_jobid_t id,
                 const char *path,
                 int flags)
{
    struct guest_watch_ctx *gw = NULL;

    if (!(gw = guest_watch_ctx_create (ctx, msg, id, path, flags)))
        goto error;

    if (get_main_eventlog (gw) < 0)
        goto error;

    if (zlist_append (ctx->guest_watchers, gw) < 0) {
        flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->guest_watchers, gw, guest_watch_ctx_destroy, true);
    gw = NULL;

    return 0;

error:
    guest_watch_ctx_destroy (gw);
    return -1;
}

/* Cancel guest_watch 'gw' if it matches message
 */
static void guest_watch_cancel (struct info_ctx *ctx,
                                struct guest_watch_ctx *gw,
                                const flux_msg_t *msg,
                                bool cancel)
{
    bool match;
    if (cancel)
        match = flux_cancel_match (msg, gw->msg);
    else
        match = flux_disconnect_match (msg, gw->msg);
    if (match)
        send_eventlog_watch_cancel (gw, NULL, cancel);
}

void guest_watchers_cancel (struct info_ctx *ctx,
                            const flux_msg_t *msg,
                            bool cancel)
{
    struct guest_watch_ctx *gw;

    gw = zlist_first (ctx->guest_watchers);
    while (gw) {
        guest_watch_cancel (ctx, gw, msg, cancel);
        gw = zlist_next (ctx->guest_watchers);
    }
}

void guest_watch_cleanup (struct info_ctx *ctx)
{
    struct guest_watch_ctx *gw;

    while ((gw = zlist_pop (ctx->guest_watchers))) {
        send_eventlog_watch_cancel (gw, NULL, false);

        if (flux_respond_error (ctx->h, gw->msg, ENOSYS, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error",
                            __FUNCTION__);
        guest_watch_ctx_destroy (gw);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
