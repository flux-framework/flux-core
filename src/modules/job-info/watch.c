/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watch.c - handle job-info.eventlog-watch &
 * job-info.eventlog-watch-cancel for job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job.h"
#include "ccan/str/str.h"

#include "job-info.h"
#include "watch.h"
#include "guest_watch.h"
#include "allow.h"
#include "util.h"

struct watch_ctx {
    struct info_ctx *ctx;
    const flux_msg_t *msg;
    flux_jobid_t id;
    bool guest;
    char *path;
    int flags;
    flux_future_t *check_f;
    flux_future_t *watch_f;
    bool allow;
    bool kvs_watch_canceled;
    bool cancel;
};

static void watch_continuation (flux_future_t *f, void *arg);
static void check_eventlog_continuation (flux_future_t *f, void *arg);

static void watch_ctx_destroy (void *data)
{
    if (data) {
        struct watch_ctx *ctx = data;
        int save_errno = errno;
        flux_msg_decref (ctx->msg);
        free (ctx->path);
        flux_future_destroy (ctx->check_f);
        flux_future_destroy (ctx->watch_f);
        free (ctx);
        errno = save_errno;
    }
}

static struct watch_ctx *watch_ctx_create (struct info_ctx *ctx,
                                           const flux_msg_t *msg,
                                           flux_jobid_t id,
                                           bool guest,
                                           const char *path,
                                           int flags)
{
    struct watch_ctx *w = calloc (1, sizeof (*w));

    if (!w)
        return NULL;

    w->ctx = ctx;
    w->id = id;
    w->guest = guest;
    if (!(w->path = strdup (path))) {
        errno = ENOMEM;
        goto error;
    }
    w->flags = flags;

    w->msg = flux_msg_incref (msg);

    return w;

error:
    watch_ctx_destroy (w);
    return NULL;
}

static int check_eventlog (struct watch_ctx *w)
{
    char key[64];

    if (flux_job_kvs_key (key, sizeof (key), w->id, "eventlog") < 0) {
        flux_log_error (w->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        return -1;
    }

    if (!(w->check_f = flux_kvs_lookup (w->ctx->h, NULL, 0, key))) {
        flux_log_error (w->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (w->check_f, -1, check_eventlog_continuation, w) < 0) {
        /* future cleanup handled in context destruction */
        flux_log_error (w->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

static int watch_key (struct watch_ctx *w)
{
    char fullpath[128];
    char ns[128];
    char *nsptr = NULL;
    char *pathptr = NULL;
    int flags = (FLUX_KVS_WATCH | FLUX_KVS_WATCH_APPEND);

    if (w->flags & FLUX_JOB_EVENT_WATCH_WAITCREATE)
        flags |= FLUX_KVS_WAITCREATE;

    if (w->guest) {
        if (flux_job_kvs_namespace (ns, sizeof (ns), w->id) < 0) {
            flux_log_error (w->ctx->h, "%s: flux_job_kvs_namespace",
                            __FUNCTION__);
            return -1;
        }
        nsptr = ns;
        pathptr = w->path;
    }
    else {
        if (flux_job_kvs_key (fullpath,
                              sizeof (fullpath),
                              w->id,
                              w->path) < 0) {
            flux_log_error (w->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
            return -1;
        }
        pathptr = fullpath;
    }

    if (!(w->watch_f = flux_kvs_lookup (w->ctx->h, nsptr, flags, pathptr))) {
        flux_log_error (w->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (w->watch_f, -1, watch_continuation, w) < 0) {
        /* future cleanup handled in context destruction */
        flux_log_error (w->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void check_eventlog_continuation (flux_future_t *f, void *arg)
{
    struct watch_ctx *w = arg;
    struct info_ctx *ctx = w->ctx;
    const char *s;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (!w->allow) {
        if (eventlog_allow (ctx, w->msg, w->id, s) < 0)
            goto error;
        w->allow = true;
    }

    /* There is a chance user canceled before we began legitimately
     * "watching" the desired eventlog */
    if (w->kvs_watch_canceled) {
        if (w->cancel) {
            if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        }
        goto done;
    }

    if (watch_key (w) < 0)
        goto error;

    return;

error:
    if (flux_respond_error (ctx->h, w->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
done:
    /* flux future destroyed in watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->watchers, w);
}

static int check_eventlog_end (struct watch_ctx *w,
                               const char *tok,
                               size_t toklen)
{
    const char *name;
    json_t *entry = NULL;
    int rc = 0;

    if (parse_eventlog_entry (w->ctx->h, tok, toklen, &entry, &name, NULL) < 0)
        return -1;

    if (streq (name, "clean"))
        rc = 1;
    json_decref (entry);
    return rc;
}

static void watch_continuation (flux_future_t *f, void *arg)
{
    struct watch_ctx *w = arg;
    struct info_ctx *ctx = w->ctx;
    const char *s;
    const char *input;
    const char *tok;
    size_t toklen;
    const char *errmsg = NULL;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno != ENOENT && errno != ENODATA && errno != ENOTSUP)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        if (errno == ENODATA && w->kvs_watch_canceled && !w->cancel)
            goto cleanup;
        goto error;
    }

    /* Issue #4612 - zero length append illegal for an eventlog.  This
     * most likely occurred through an illegal overwrite of the whole
     * eventlog.
     */
    if (!s) {
        errmsg = "illegal append of zero bytes";
        errno = EINVAL;
        goto error;
    }

    if (w->kvs_watch_canceled) {
        if (w->cancel) {
            errno = ENODATA;
            goto error;
        }
        goto cleanup;
    }

    if (!w->allow) {
        if (eventlog_allow (ctx, w->msg, w->id, s) < 0)
            goto error_cancel;
        w->allow = true;
    }

    input = s;
    while (get_next_eventlog_entry (&input, &tok, &toklen)) {
        if (flux_respond_pack (ctx->h,
                               w->msg,
                               "{s:s#}",
                               "event", tok, toklen) < 0) {
            flux_log_error (ctx->h,
                            "%s: flux_respond_pack",
                            __FUNCTION__);
            goto error_cancel;
        }

        /* When watching the main job eventlog, we return ENODATA back
         * to the user when the eventlog has reached the end.
         *
         * An alternate main KVS namespace eventlog does not have a
         * known ruleset, so it will hang.
         */
        if (!w->guest && streq (w->path, "eventlog")) {
            if (check_eventlog_end (w, tok, toklen) > 0) {
                if (flux_kvs_lookup_cancel (w->watch_f) < 0) {
                    flux_log_error (ctx->h,
                                    "%s: flux_kvs_lookup_cancel",
                                    __FUNCTION__);
                    goto error;
                }
                /* If by small chance there is an event after "clean"
                 * (e.g. user appended), we won't send it */
                errno = ENODATA;
                goto error;
            }
        }
    }

    flux_future_reset (f);
    return;

error_cancel:
    /* If we haven't sent a cancellation yet, must do so so that
     * the future's matchtag will eventually be freed */
    if (!w->kvs_watch_canceled) {
        int save_errno = errno;
        if (flux_kvs_lookup_cancel (w->watch_f) < 0)
            flux_log_error (ctx->h,
                            "%s: flux_kvs_lookup_cancel",
                            __FUNCTION__);
        errno = save_errno;
    }

error:
    if (flux_respond_error (ctx->h, w->msg, errno, errmsg) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
cleanup:
    /* flux future destroyed in watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->watchers, w);
}

static int watch (struct info_ctx *ctx,
                  const flux_msg_t *msg,
                  flux_jobid_t id,
                  const char *path,
                  int flags,
                  bool guest)
{
    struct watch_ctx *w = NULL;
    uint32_t rolemask;

    if (!(w = watch_ctx_create (ctx, msg, id, guest, path, flags)))
        goto error;

    /* if user requested an alternate path and that alternate path is
     * not the main eventlog, we have to check the main eventlog for
     * access first.
     *
     * if rpc from owner, no need to do guest access check.  Likewise
     * if the cached check indicates we can read the alternate path.
     */

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;

    if ((rolemask & FLUX_ROLE_OWNER))
        w->allow = true;

    if (!w->allow) {
        int ret;
        if ((ret = eventlog_allow_lru (w->ctx,
                                       w->msg,
                                       w->id)) < 0)
            return -1;

        if (ret)
            w->allow = true;
    }

    if (path
        && !streq (path, "eventlog")
        && !w->allow) {
        if (check_eventlog (w) < 0)
            goto error;
    }
    else {
        if (watch_key (w) < 0)
            goto error;
    }

    if (zlist_append (ctx->watchers, w) < 0) {
        flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->watchers, w, watch_ctx_destroy, true);
    w = NULL;

    return 0;

error:
    watch_ctx_destroy (w);
    return -1;
}

void watch_cb (flux_t *h,
               flux_msg_handler_t *mh,
               const flux_msg_t *msg,
               void *arg)
{
    struct info_ctx *ctx = arg;
    struct watch_ctx *w = NULL;
    flux_jobid_t id;
    int guest = 0;
    const char *path = NULL;
    int flags;
    int valid_flags = FLUX_JOB_EVENT_WATCH_WAITCREATE;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:s s:i}",
                             "id", &id,
                             "path", &path,
                             "flags", &flags) < 0)
        goto error;
    if ((flags & ~valid_flags)) {
        errno = EPROTO;
        errmsg = "eventlog-watch request rejected with invalid flag";
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "eventlog-watch request rejected without streaming RPC flag";
        goto error;
    }
    /* guest flag indicates to read path from guest namespace */
    (void)flux_request_unpack (msg, NULL, "{s:b}", "guest", &guest);

    /* if watching a "guest" path, forward to guest watcher for
     * handling */
    if (strstarts (path, "guest.")) {
        if (guest_watch (ctx, msg, id, path + 6, flags) < 0)
            goto error;
    }
    else {
        if (watch (ctx, msg, id, path, flags, guest) < 0)
            goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    watch_ctx_destroy (w);
}

/* Cancel watch 'w' if it matches message.
 */
static void send_kvs_watch_cancel (struct info_ctx *ctx,
                                   struct watch_ctx *w,
                                   const flux_msg_t *msg,
                                   bool cancel)
{
    bool match;
    if (cancel)
        match = flux_cancel_match (msg, w->msg);
    else
        match = flux_disconnect_match (msg, w->msg);
    if (match) {
        w->kvs_watch_canceled = true;
        w->cancel = cancel;

        /* if the watching hasn't started yet, no need to cancel */
        if (w->watch_f) {
            if (flux_kvs_lookup_cancel (w->watch_f) < 0) {
                flux_log_error (ctx->h,
                                "%s: flux_kvs_lookup_cancel",
                                __FUNCTION__);
            }
        }
    }
}

void watchers_cancel (struct info_ctx *ctx, const flux_msg_t *msg, bool cancel)
{
    struct watch_ctx *w;

    w = zlist_first (ctx->watchers);
    while (w) {
        send_kvs_watch_cancel (ctx, w, msg, cancel);
        w = zlist_next (ctx->watchers);
    }
}

void watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    watchers_cancel (ctx, msg, true);
    guest_watchers_cancel (ctx, msg, true);
}

void watch_cleanup (struct info_ctx *ctx)
{
    struct watch_ctx *w;

    while ((w = zlist_pop (ctx->watchers))) {
        if (w->watch_f) {
            if (flux_kvs_lookup_cancel (w->watch_f) < 0) {
                flux_log_error (ctx->h,
                                "%s: flux_kvs_lookup_cancel",
                                __FUNCTION__);
            }
        }
        if (flux_respond_error (ctx->h, w->msg, ENOSYS, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        watch_ctx_destroy (w);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
