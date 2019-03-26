/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

/* Module state.
 */
struct info_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zlist_t *lookups;
};

/* Lookup context
 */
struct lookup_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    int flags;
    int lookup_flags;
    bool active;
    flux_future_t *f;
    int offset;
    bool allow;
    bool cancel;
};

void lookup_ctx_destroy (void *data)
{
    if (data) {
        struct lookup_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

static void lookup_continuation (flux_future_t *f, void *arg);

struct lookup_ctx *lookup_ctx_create (struct info_ctx *ctx,
                                      const flux_msg_t *msg,
                                      flux_jobid_t id,
                                      int flags)
{
    struct lookup_ctx *l = calloc (1, sizeof (*l));
    int saved_errno;

    if (!l)
        return NULL;

    l->ctx = ctx;
    l->id = id;
    l->flags = flags;
    l->active = true;

    if (l->flags & FLUX_JOB_INFO_WATCH) {
        l->lookup_flags |= FLUX_KVS_WATCH;
        l->lookup_flags |= FLUX_KVS_WATCH_APPEND;
    }

    if (!(l->msg = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    return l;

error:
    saved_errno = errno;
    lookup_ctx_destroy (l);
    errno = saved_errno;
    return NULL;
}

/* 'pp' is an in/out parameter pointing to input buffer.
 * Set 'tok' to next \n-terminated token, and 'toklen' to its length.
 * Advance 'pp' past token.  Returns false when input is exhausted.
 */
static bool eventlog_parse_next (const char **pp, const char **tok,
                                 size_t *toklen)
{
    char *term;

    if (!(term = strchr (*pp, '\n')))
        return false;
    *tok = *pp;
    *toklen = term - *pp + 1;
    *pp = term + 1;
    return true;
}

static int lookup_key (struct lookup_ctx *l)
{
    char key[64];

    if (l->f) {
        flux_future_destroy (l->f);
        l->f = NULL;
    }

    if (flux_job_kvs_key (key, sizeof (key), l->active, l->id, "eventlog") < 0) {
        flux_log_error (l->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        return -1;
    }

    if (!(l->f = flux_kvs_lookup (l->ctx->h, NULL, l->lookup_flags, key))) {
        flux_log_error (l->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (l->f, -1, lookup_continuation, l) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

/* Parse the submit userid from the event log.
 * Assume "submit" is the first event.
 */
static int eventlog_get_userid (struct lookup_ctx *l,
                                const char *s, int *useridp)
{
    const char *input = s;
    const char *tok;
    size_t toklen;
    char *event = NULL;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    json_t *o = NULL;
    int save_errno;

    if (!eventlog_parse_next (&input, &tok, &toklen)) {
        flux_log_error (l->ctx->h, "%s: invalid event", __FUNCTION__);
        errno = EINVAL;
        goto error;
    }
    if (!(event = strndup (tok, toklen)))
        goto error;
    if (flux_kvs_event_decode (event, NULL, name, sizeof (name),
                               context, sizeof (context)) < 0)
        goto error;
    if (strcmp (name, "submit") != 0) {
        flux_log_error (l->ctx->h, "%s: invalid event", __FUNCTION__);
        errno = EINVAL;
        goto error;
    }
    if (!(o = json_loads (context, 0, NULL))) {
        errno = EPROTO;
        goto error;
    }
    if (json_unpack (o, "{ s:i }", "userid", useridp) < 0) {
        errno = EPROTO;
        goto error;
    }
    free (event);
    json_decref (o);
    return 0;
 error:
    save_errno = errno;
    free (event);
    json_decref (o);
    errno = save_errno;
    return -1;
}


/* Determine if user who sent request 'msg' is allowed to
 * access job eventlog 's'.  Assume first event is the "submit"
 * event which records the job owner.
 */
static int lookup_allow (struct lookup_ctx *l, const char *s)
{
    uint32_t userid;
    uint32_t rolemask;
    int job_user;

    if (flux_msg_get_rolemask (l->msg, &rolemask) < 0)
        return -1;
    if (!(rolemask & FLUX_ROLE_OWNER)) {
        if (flux_msg_get_userid (l->msg, &userid) < 0)
            return -1;
        if (eventlog_get_userid (l, s, &job_user) < 0)
            return -1;
        if (userid != job_user) {
            errno = EPERM;
            return -1;
        }
    }
    return 0;
}

static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct lookup_ctx *l = arg;
    struct info_ctx *ctx = l->ctx;
    const char *s;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno == ENOENT && l->active) {
            /* transition / try the inactive key */
            l->active = false;
            if (lookup_key (l) < 0)
                goto error;
            return;
        }
        else if (errno == ENODATA && (l->flags & FLUX_JOB_INFO_WATCH)) {
            if (flux_respond_error (ctx->h, l->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            goto done;
        }
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (l->cancel) {
        if ((l->flags & FLUX_JOB_INFO_WATCH)) {
            if (flux_respond_error (ctx->h, l->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        }
        goto done;
    }

    if (!l->allow) {
        if (lookup_allow (l, s) < 0)
            goto error;
        l->allow = true;
    }

    if ((l->flags & FLUX_JOB_INFO_WATCH)) {
        const char *input = s;
        const char *tok;
        size_t toklen;
        while (eventlog_parse_next (&input, &tok, &toklen)) {
            if (l->active)
                l->offset += toklen;

            if (l->active || !l->offset) {
                if (flux_respond_pack (ctx->h, l->msg,
                                       "{s:s#}",
                                       "event", tok, toklen) < 0) {
                    flux_log_error (ctx->h, "%s: flux_respond_pack",
                                    __FUNCTION__);
                    goto error;
                }
            }

            if (!l->active && l->offset)
                l->offset -= toklen;
        }

        if (l->active)
            flux_future_reset (f);
        else {
            /* we're in inactive state, there are no more events coming */
            if (flux_respond_error (ctx->h, l->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            goto done;
        }
    }
    else {
        if (flux_respond_pack (ctx->h, l->msg, "{s:s}", "event", s) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
        goto done;
    }

    return;

error:
    if (flux_respond_error (ctx->h, l->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
done:
    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->lookups, l);
}

static void lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct lookup_ctx *l = NULL;
    flux_jobid_t id;
    int flags;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                             "id", &id,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(l = lookup_ctx_create (ctx, msg, id, flags)))
        goto error;

    if (lookup_key (l) < 0)
        goto error;

    if (zlist_append (ctx->lookups, l) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->lookups, l, lookup_ctx_destroy, true);
    l = NULL;

    return;

error:
    lookup_ctx_destroy (l);
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Cancel lookup 'l' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 */
static void lookup_cancel (struct info_ctx *ctx,
                           struct lookup_ctx *l,
                           const char *sender, uint32_t matchtag)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
        && (flux_msg_get_matchtag (l->msg, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (l->msg, &s) < 0)
        return;
    if (!strcmp (sender, s)) {
        if ((l->flags & FLUX_JOB_INFO_WATCH)) {
            if (flux_kvs_lookup_cancel (l->f) < 0)
                flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                                __FUNCTION__);
        }
        l->cancel = true;
    }
    free (s);
}

/* Cancel all lookups that match (sender, matchtag). */
static void lookups_cancel (struct info_ctx *ctx,
                            const char *sender, uint32_t matchtag)
{
    struct lookup_ctx *l;

    l = zlist_first (ctx->lookups);
    while (l) {
        lookup_cancel (ctx, l, sender, matchtag);
        l = zlist_next (ctx->lookups);
    }
}

static void cancel_cb (flux_t *h, flux_msg_handler_t *mh,
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
    lookups_cancel (ctx, sender, matchtag);
    free (sender);
}

static void disconnect_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    char *sender;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: flux_request_decode", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    lookups_cancel (ctx, sender, FLUX_MATCHTAG_NONE);
    free (sender);
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;

    if (flux_respond_pack (h, msg, "{s:i}",
                           "lookups", zlist_size (ctx->lookups)) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.eventlog-lookup",
      .cb           = lookup_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.eventlog-cancel",
      .cb           = cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void info_ctx_destroy (struct info_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->lookups) {
            struct lookup_ctx *l;

            while ((l = zlist_pop (ctx->lookups))) {
                if ((l->flags & FLUX_JOB_INFO_WATCH)) {
                    if (flux_kvs_lookup_cancel (l->f) < 0)
                        flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                                        __FUNCTION__);

                    if (flux_respond_error (ctx->h, l->msg, ENOSYS, NULL) < 0)
                        flux_log_error (ctx->h, "%s: flux_respond_error",
                                        __FUNCTION__);
                }
                lookup_ctx_destroy (l);
            }
            zlist_destroy (&ctx->lookups);
        }
        free (ctx);
        errno = saved_errno;
    }
}

static struct info_ctx *info_ctx_create (flux_t *h)
{
    struct info_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->lookups = zlist_new ()))
        goto error;
    return ctx;
error:
    info_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct info_ctx *ctx;
    int rc = -1;

    if (!(ctx = info_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    info_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("job-info");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
