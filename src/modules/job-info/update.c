/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* update.c - handle job-info.update-watch &
 * job-info.eventlog-update-cancel for job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"

#include "job-info.h"
#include "update.h"
#include "util.h"

typedef enum {
    UPDATE_TYPE_LOOKUP,
    UPDATE_TYPE_WATCH,
} update_type_t;

struct update_ctx {
    struct info_ctx *ctx;
    update_type_t type;
    struct flux_msglist *msglist;
    uint32_t userid;
    flux_jobid_t id;
    char *key;
    int flags;
    const char *update_name;
    flux_future_t *lookup_f;
    flux_future_t *eventlog_watch_f;
    bool eventlog_watch_canceled;
    json_t *update_object;
    int initial_update_count;
    int watch_update_count;
    char *index_key;            /* for watch */
};

static void update_ctx_destroy (void *data)
{
    if (data) {
        struct update_ctx *uc = data;
        int save_errno = errno;
        flux_msglist_destroy (uc->msglist);
        free (uc->key);
        flux_future_destroy (uc->lookup_f);
        flux_future_destroy (uc->eventlog_watch_f);
        json_decref (uc->update_object);
        free (uc->index_key);
        free (uc);
        errno = save_errno;
    }
}

static char *get_index_key (flux_jobid_t id, const char *key)
{
    char *s;
    if (asprintf (&s, "%ju-%s", id, key) < 0)
        return NULL;
    return s;
}

static struct update_ctx *update_ctx_create (struct info_ctx *ctx,
                                             update_type_t type,
                                             const flux_msg_t *msg,
                                             flux_jobid_t id,
                                             const char *key,
                                             int flags)
{
    struct update_ctx *uc = calloc (1, sizeof (*uc));

    if (!uc)
        return NULL;

    uc->ctx = ctx;
    uc->type = type;
    uc->id = id;
    if (!(uc->key = strdup (key)))
        goto error;
    if (streq (key, "R"))
        uc->update_name = "resource-update";
    else {
        errno = EINVAL;
        goto error;
    }
    uc->flags = flags;

    /* for lookups, the msglist will never be > 1 in length */
    if (!(uc->msglist = flux_msglist_create ()))
        goto error;
    flux_msglist_append (uc->msglist, msg);

    if (uc->type == UPDATE_TYPE_WATCH) {
        /* use jobid + key as lookup key, in future we may support other
         * keys other than R
         */
        if (!(uc->index_key = get_index_key (uc->id, uc->key)))
            goto error;
    }

    return uc;

error:
    update_ctx_destroy (uc);
    return NULL;
}

static void eventlog_watch_cancel (struct update_ctx *uc)
{
    flux_future_t *f;
    int matchtag;

    /* in some cases, possible eventlog watch hasn't started yet */
    if (!uc->eventlog_watch_f || uc->eventlog_watch_canceled)
        return;

    matchtag = (int)flux_rpc_get_matchtag (uc->eventlog_watch_f);

    if (!(f = flux_rpc_pack (uc->ctx->h,
                             "job-info.eventlog-watch-cancel",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:i}",
                             "matchtag", matchtag))) {
        flux_log_error (uc->ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        return;
    }
    flux_future_destroy (f);
    uc->eventlog_watch_canceled = true;
}

static void apply_updates_R (struct update_ctx *uc,
                             json_t *context)
{
    const char *key;
    json_t *value;

    json_object_foreach (context, key, value) {
        /* RFC 21 resource-update event only allows update
         * to:
         * - expiration
         */
        if (streq (key, "expiration"))
            if (jpath_set (uc->update_object,
                           "execution.expiration",
                           value) < 0)
                flux_log (uc->ctx->h, LOG_INFO,
                          "%s: failed to update job %s %s",
                          __FUNCTION__, idf58 (uc->id), uc->key);
    }
}

static void eventlog_continuation (flux_future_t *f, void *arg)
{
    struct update_ctx *uc = arg;
    struct info_ctx *ctx = uc->ctx;
    const char *s;
    json_t *event = NULL;
    const char *name;
    json_t *context = NULL;
    const char *errmsg = NULL;
    const flux_msg_t *msg;

    if (flux_rpc_get (f, NULL) < 0) {
        /* ENODATA is normal when job finishes or we've sent cancel */
        if (errno != ENODATA)
            flux_log_error (ctx->h, "%s: job-info.eventlog-watch", __FUNCTION__);
        goto error;
    }

    /* if count == 0, all callers canceled streams */
    if (flux_msglist_count (uc->msglist) == 0)
        goto cleanup;

    if (flux_job_event_watch_get (f, &s) < 0) {
        flux_log_error (ctx->h, "%s: flux_job_event_watch_get", __FUNCTION__);
        goto error_cancel;
    }

    if (!(event = eventlog_entry_decode (s))) {
        flux_log_error (uc->ctx->h, "%s: eventlog_entry_decode", __FUNCTION__);
        goto error_cancel;
    }

    if (eventlog_entry_parse (event, NULL, &name, &context) < 0) {
        flux_log_error (uc->ctx->h, "%s: eventlog_entry_decode", __FUNCTION__);
        goto error_cancel;
    }

    if (context && streq (name, uc->update_name)) {
        uc->watch_update_count++;

        /* don't apply update events that we've already applied from
         * initial lookup */
        if (uc->watch_update_count > uc->initial_update_count) {
            if (streq (uc->key, "R"))
                apply_updates_R (uc, context);

            msg = flux_msglist_first (uc->msglist);
            while (msg) {
                if (flux_respond_pack (uc->ctx->h, msg, "{s:O}",
                                       uc->key, uc->update_object) < 0) {
                    flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
                    goto error_cancel;
                }
                msg = flux_msglist_next (uc->msglist);
            }
        }
    }

    flux_future_reset (f);
    json_decref (event);
    return;

error_cancel:
    /* Must do so so that the future's matchtag will eventually be
     * freed */
    eventlog_watch_cancel (uc);

error:
    msg = flux_msglist_first (uc->msglist);
    while (msg) {
        if (flux_respond_error (ctx->h, msg, errno, errmsg) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        msg = flux_msglist_next (uc->msglist);
    }

cleanup:
    /* flux future destroyed in update_ctx_destroy, which is
     * called via zlist_remove() */
    zhashx_delete (ctx->index_uw, uc->index_key);
    zlist_remove (ctx->update_watchers, uc);
}

static int eventlog_watch (struct update_ctx *uc)
{
    const char *topic = "job-info.eventlog-watch";
    int rpc_flags = FLUX_RPC_STREAMING;
    flux_msg_t *msg = NULL;
    int save_errno, rc = -1;

    if (!(uc->eventlog_watch_f = flux_rpc_pack (uc->ctx->h,
                                                topic,
                                                FLUX_NODEID_ANY,
                                                rpc_flags,
                                                "{s:I s:s s:i}",
                                                "id", uc->id,
                                                "path", "eventlog",
                                                "flags", 0))) {
        flux_log_error (uc->ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (uc->eventlog_watch_f,
                          -1,
                          eventlog_continuation,
                          uc) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (uc->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    rc = 0;
error:
    save_errno = errno;
    flux_msg_destroy (msg);
    errno = save_errno;
    return rc;
}

static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct update_ctx *uc = arg;
    struct info_ctx *ctx = uc->ctx;
    const char *key_str;
    const char *eventlog_str;
    const char *input;
    const char *tok;
    size_t toklen;
    const char *errmsg = NULL;
    bool job_ended = false;
    bool submit_parsed = false;
    const flux_msg_t *msg;

    if (flux_rpc_get_unpack (f, "{s:s s:s}",
                             uc->key, &key_str,
                             "eventlog", &eventlog_str) < 0) {
        if (errno != ENOENT && errno != EPERM)
            flux_log_error (ctx->h, "%s: flux_rpc_get_unpack", __FUNCTION__);
        goto error;
    }

    /* if count == 0, all callers canceled streams */
    if (flux_msglist_count (uc->msglist) == 0)
        goto cleanup;

    if (!(uc->update_object = json_loads (key_str, 0, NULL))) {
        errno = EINVAL;
        errmsg = "lookup value cannot be parsed";
        goto error;
    }

    input = eventlog_str;
    while (eventlog_parse_next (&input, &tok, &toklen)) {
        json_t *entry = NULL;
        const char *name;
        json_t *context = NULL;
        if (eventlog_parse_entry_chunk (uc->ctx->h,
                                        tok,
                                        toklen,
                                        &entry,
                                        &name,
                                        &context) < 0) {
            errmsg = "error parsing eventlog";
            goto error;
        }
        if (streq (name, "submit")) {
            if (!context) {
                errno = EPROTO;
                goto error;
            }
            if (json_unpack (context, "{ s:i }", "userid", &uc->userid) < 0) {
                errno = EPROTO;
                goto error;
            }
            submit_parsed = true;
        }
        else if (streq (name, uc->update_name)) {
            if (streq (uc->key, "R"))
                apply_updates_R (uc, context);
            uc->initial_update_count++;
        }
        else if (streq (name, "clean"))
            job_ended = true;
        json_decref (entry);
    }

    /* double check, generally speaking should be impossible */
    if (!submit_parsed) {
        errno = EPROTO;
        goto error;
    }

    msg = flux_msglist_first (uc->msglist);
    while (msg) {
        /* caller can't access this data, this is not a "fatal" error,
         * so send error to this one message and continue on the
         * msglist
         */
        if (flux_msg_authorize (msg, uc->userid) < 0) {
            if (flux_respond_error (ctx->h, msg, errno, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            flux_msglist_delete (uc->msglist);
            goto next;
        }

        if (flux_respond_pack (uc->ctx->h, msg, "{s:O}",
                               uc->key, uc->update_object) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
            goto error;
        }

    next:
        msg = flux_msglist_next (uc->msglist);
    }

    /* due to security check above, possible no more messages in this
     * watcher */
    if (flux_msglist_count (uc->msglist) == 0)
        goto cleanup;

    /* If this is a lookup, we're done, only continue on if this is a
     * watcher */
    if (uc->type == UPDATE_TYPE_LOOKUP)
        goto cleanup;

    /* this job has ended, no need to watch the eventlog for future
     * updates */
    if (job_ended) {
        errno = ENODATA;
        goto error;
    }

    /* we've confirmed key is readable and sent to caller, now watch
     * eventlog for future changes */
    if (eventlog_watch (uc) < 0)
        goto error;

    return;

error:
    msg = flux_msglist_first (uc->msglist);
    while (msg) {
        if (flux_respond_error (ctx->h, msg, errno, errmsg) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        msg = flux_msglist_next (uc->msglist);
    }

cleanup:
    /* flux future destroyed in update_ctx_destroy, which is called
     * via zlist_remove() */
    if (uc->type == UPDATE_TYPE_WATCH) {
        zhashx_delete (ctx->index_uw, uc->index_key);
        zlist_remove (ctx->update_watchers, uc);
    }
    else
        zlist_remove (ctx->update_lookups, uc);
}

static int update_lookup (struct info_ctx *ctx,
                          update_type_t type,
                          const flux_msg_t *msg,
                          flux_jobid_t id,
                          const char *key,
                          int flags)
{
    struct update_ctx *uc = NULL;
    const char *topic = "job-info.lookup";

    if (!(uc = update_ctx_create (ctx,
                                  type,
                                  msg,
                                  id,
                                  key,
                                  flags)))
        goto error;

    if (!(uc->lookup_f = flux_rpc_pack (uc->ctx->h,
                                        topic,
                                        FLUX_NODEID_ANY,
                                        0,
                                        "{s:I s:[ss] s:i}",
                                        "id", uc->id,
                                        "keys", uc->key, "eventlog",
                                        "flags", 0))) {
        flux_log_error (uc->ctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (uc->lookup_f,
                          -1,
                          lookup_continuation,
                          uc) < 0) {
        /* future cleanup handled with context destruction */
        flux_log_error (uc->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    if (type == UPDATE_TYPE_WATCH) {
        if (zlist_append (ctx->update_watchers, uc) < 0) {
            flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
            goto error;
        }
        zlist_freefn (ctx->update_watchers, uc, update_ctx_destroy, true);

        if (zhashx_insert (ctx->index_uw, uc->index_key, uc) < 0) {
            flux_log_error (ctx->h, "%s: zhashx_insert", __FUNCTION__);
            goto error_list;
        }
    }
    else {
        if (zlist_append (ctx->update_lookups, uc) < 0) {
            flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
            goto error;
        }
        zlist_freefn (ctx->update_lookups, uc, update_ctx_destroy, true);
    }

    return 0;

error_list:
    zlist_remove (ctx->update_watchers, uc);
    return -1;

error:
    update_ctx_destroy (uc);
    return -1;
}

void update_lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct update_ctx *uc = NULL;
    flux_jobid_t id;
    const char *key = NULL;
    int flags;
    int valid_flags = 0;
    const char *errmsg = NULL;
    char *index_key = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:s s:i}",
                             "id", &id,
                             "key", &key,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if ((flags & ~valid_flags)) {
        errno = EPROTO;
        errmsg = "update-lookup request rejected with invalid flag";
        goto error;
    }
    if (!streq (key, "R")) {
        errno = EINVAL;
        errmsg = "update-lookup unsupported key specified";
        goto error;
    }

    if (!(index_key = get_index_key (id, key)))
        goto error;

    /* is somebody watching this jobid + key already, return the
     * cached result */

    if ((uc = zhashx_lookup (ctx->index_uw, index_key))
        && uc->update_object) {
        if (flux_msg_authorize (msg, uc->userid) < 0)
            goto error;
        if (flux_respond_pack (uc->ctx->h, msg, "{s:O}",
                               uc->key, uc->update_object) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
            goto error;
        }
    }
    else {
        if (update_lookup (ctx,
                           UPDATE_TYPE_LOOKUP,
                           msg,
                           id,
                           key,
                           flags) < 0)
            goto error;
    }

    free (index_key);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (index_key);
}

void update_watch_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct update_ctx *uc = NULL;
    flux_jobid_t id;
    const char *key = NULL;
    int flags;
    int valid_flags = 0;
    const char *errmsg = NULL;
    char *index_key = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:s s:i}",
                             "id", &id,
                             "key", &key,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if ((flags & ~valid_flags)) {
        errno = EPROTO;
        errmsg = "update-watch request rejected with invalid flag";
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "update-watch request rejected without streaming RPC flag";
        goto error;
    }
    if (!streq (key, "R")) {
        errno = EINVAL;
        errmsg = "update-watch unsupported key specified";
        goto error;
    }

    if (!(index_key = get_index_key (id, key)))
        goto error;

    /* if no watchers for this jobid yet, start it */
    if (!(uc = zhashx_lookup (ctx->index_uw, index_key))) {
        if (update_lookup (ctx,
                           UPDATE_TYPE_WATCH,
                           msg,
                           id,
                           key,
                           flags) < 0)
            goto error;
    }
    else {
        if (uc->update_object) {
            if (flux_msg_authorize (msg, uc->userid) < 0)
                goto error;
            if (flux_respond_pack (uc->ctx->h, msg, "{s:O}",
                                   uc->key, uc->update_object) < 0) {
                flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
                goto error;
            }
        }
        /* if uc->update_object has not been set, the initial lookup
         * has not completed.  The security check will be done in
         * watch_lookup_continuation when the initial lookup completes.
         */
        flux_msglist_append (uc->msglist, msg);
    }

    free (index_key);
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (index_key);
}

/* Cancel update_watch if it matches message.
 */
static void update_watch_cancel (struct update_ctx *uc,
                                 const flux_msg_t *msg,
                                 bool cancel)
{
    if (cancel) {
        if (flux_msglist_cancel (uc->ctx->h, uc->msglist, msg) < 0)
            flux_log_error (uc->ctx->h,
                            "error handling job-info.update-watch-cancel");
    }
    else {
        if (flux_msglist_disconnect (uc->msglist, msg) < 0)
            flux_log_error (uc->ctx->h,
                            "error handling job-info.update-watch disconnect");
    }

    if (flux_msglist_count (uc->msglist) == 0)
        eventlog_watch_cancel (uc);
}

void update_watchers_cancel (struct info_ctx *ctx, const flux_msg_t *msg, bool cancel)
{
    struct update_ctx *uc;

    uc = zlist_first (ctx->update_watchers);
    while (uc) {
        update_watch_cancel (uc, msg, cancel);
        uc = zlist_next (ctx->update_watchers);
    }
}

void update_watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    update_watchers_cancel (ctx, msg, true);
}

void update_watch_cleanup (struct info_ctx *ctx)
{
    struct update_ctx *uc;

    while ((uc = zlist_pop (ctx->update_watchers))) {
        const flux_msg_t *msg;
        eventlog_watch_cancel (uc);
        msg = flux_msglist_first (uc->msglist);
        while (msg) {
            if (flux_respond_error (ctx->h, msg, ENOSYS, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error",
                                __FUNCTION__);
            msg = flux_msglist_next (uc->msglist);
        }
        update_ctx_destroy (uc);
    }
}

int update_watch_count (struct info_ctx *ctx)
{
    struct update_ctx *uc;
    int count = 0;

    uc = zlist_first (ctx->update_watchers);
    while (uc) {
        count += flux_msglist_count (uc->msglist);
        uc = zlist_next (ctx->update_watchers);
    }
    return count;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
