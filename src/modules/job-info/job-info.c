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
    zlist_t *watchers;
};

/* Lookup context
 */
struct lookup_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    int flags;
    bool active;
    flux_future_t *f;
    bool allow;
};

/* Watch context
 */
struct watch_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    bool active;
    flux_future_t *f;
    int offset;
    bool allow;
    bool cancel;
};

static void lookup_continuation (flux_future_t *f, void *arg);
static void watch_continuation (flux_future_t *f, void *arg);

void lookup_ctx_destroy (void *data)
{
    if (data) {
        struct lookup_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

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

void watch_ctx_destroy (void *data)
{
    if (data) {
        struct watch_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

struct watch_ctx *watch_ctx_create (struct info_ctx *ctx,
                                    const flux_msg_t *msg,
                                    flux_jobid_t id)
{
    struct watch_ctx *w = calloc (1, sizeof (*w));
    int saved_errno;

    if (!w)
        return NULL;

    w->ctx = ctx;
    w->id = id;
    w->active = true;

    if (!(w->msg = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    return w;

error:
    saved_errno = errno;
    watch_ctx_destroy (w);
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

/* Parse the submit userid from the event log.
 * Assume "submit" is the first event.
 */
static int eventlog_get_userid (struct info_ctx *ctx, const char *s,
                                int *useridp)
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
        flux_log_error (ctx->h, "%s: invalid event", __FUNCTION__);
        errno = EINVAL;
        goto error;
    }
    if (!(event = strndup (tok, toklen)))
        goto error;
    if (flux_kvs_event_decode (event, NULL, name, sizeof (name),
                               context, sizeof (context)) < 0)
        goto error;
    if (strcmp (name, "submit") != 0) {
        flux_log_error (ctx->h, "%s: invalid event", __FUNCTION__);
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
static int eventlog_allow (struct info_ctx *ctx, const flux_msg_t *msg,
                           const char *s)
{
    uint32_t userid;
    uint32_t rolemask;
    int job_user;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return -1;
    if (!(rolemask & FLUX_ROLE_OWNER)) {
        if (flux_msg_get_userid (msg, &userid) < 0)
            return -1;
        if (eventlog_get_userid (ctx, s, &job_user) < 0)
            return -1;
        if (userid != job_user) {
            errno = EPERM;
            return -1;
        }
    }
    return 0;
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

    if (!(l->f = flux_kvs_lookup (l->ctx->h, NULL, 0, key))) {
        flux_log_error (l->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (l->f, -1, lookup_continuation, l) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
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
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (!l->allow) {
        if (eventlog_allow (ctx, l->msg, s) < 0)
            goto error;
        l->allow = true;
    }

    if (flux_respond_pack (ctx->h, l->msg, "{s:s}", "eventlog", s) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->lookups, l);
    return;

error:
    if (flux_respond_error (ctx->h, l->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

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
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    lookup_ctx_destroy (l);
}

static int watch_key (struct watch_ctx *w)
{
    char key[64];
    int flags = (FLUX_KVS_WATCH | FLUX_KVS_WATCH_APPEND);

    if (w->f) {
        flux_future_destroy (w->f);
        w->f = NULL;
    }

    if (flux_job_kvs_key (key, sizeof (key), w->active, w->id, "eventlog") < 0) {
        flux_log_error (w->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        return -1;
    }

    if (!(w->f = flux_kvs_lookup (w->ctx->h, NULL, flags, key))) {
        flux_log_error (w->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (w->f, -1, watch_continuation, w) < 0) {
        flux_log_error (w->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void watch_continuation (flux_future_t *f, void *arg)
{
    struct watch_ctx *w = arg;
    struct info_ctx *ctx = w->ctx;
    const char *s;
    const char *input;
    const char *tok;
    size_t toklen;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno == ENOENT && w->active) {
            /* transition / try the inactive key */
            w->active = false;
            if (watch_key (w) < 0)
                goto error;
            return;
        }
        else if (errno == ENODATA) {
            if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            goto done;
        }
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (w->cancel) {
        if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }

    if (!w->allow) {
        if (eventlog_allow (ctx, w->msg, s) < 0)
            goto error;
        w->allow = true;
    }

    input = s;
    while (eventlog_parse_next (&input, &tok, &toklen)) {
        if (w->active)
            w->offset += toklen;

        if (w->active || !w->offset) {
            if (flux_respond_pack (ctx->h, w->msg,
                                   "{s:s#}",
                                   "event", tok, toklen) < 0) {
                flux_log_error (ctx->h, "%s: flux_respond_pack",
                                __FUNCTION__);
                goto error;
            }
        }

        if (!w->active && w->offset)
            w->offset -= toklen;
    }

    if (w->active)
        flux_future_reset (f);
    else {
        /* we're in inactive state, there are no more events coming */
        if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }

    return;

error:
    if (flux_respond_error (ctx->h, w->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
done:
    /* flux future destroyed in watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->watchers, w);
}

static void watch_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct watch_ctx *w = NULL;
    flux_jobid_t id;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(w = watch_ctx_create (ctx, msg, id)))
        goto error;

    if (watch_key (w) < 0)
        goto error;

    if (zlist_append (ctx->watchers, w) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->watchers, w, watch_ctx_destroy, true);
    w = NULL;

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    watch_ctx_destroy (w);
}

/* Cancel watch 'w' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 */
static void watch_cancel (struct info_ctx *ctx,
                          struct watch_ctx *w,
                          const char *sender, uint32_t matchtag)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
        && (flux_msg_get_matchtag (w->msg, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (w->msg, &s) < 0)
        return;
    if (!strcmp (sender, s)) {
        if (flux_kvs_lookup_cancel (w->f) < 0)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                            __FUNCTION__);
        w->cancel = true;
    }
    free (s);
}

/* Cancel all lookups that match (sender, matchtag). */
static void watchers_cancel (struct info_ctx *ctx,
                            const char *sender, uint32_t matchtag)
{
    struct watch_ctx *w;

    w = zlist_first (ctx->watchers);
    while (w) {
        watch_cancel (ctx, w, sender, matchtag);
        w = zlist_next (ctx->watchers);
    }
}

static void watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
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
    watchers_cancel (ctx, sender, matchtag);
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
    watchers_cancel (ctx, sender, FLUX_MATCHTAG_NONE);
    free (sender);
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;

    if (flux_respond_pack (h, msg, "{s:i s:i}",
                           "lookups", zlist_size (ctx->lookups),
                           "watchers", zlist_size (ctx->watchers)) < 0) {
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
      .topic_glob   = "job-info.eventlog-watch",
      .cb           = watch_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.eventlog-watch-cancel",
      .cb           = watch_cancel_cb,
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
        /* freefn set on lookup entries will destroy list entries */
        if (ctx->lookups)
            zlist_destroy (&ctx->lookups);
        if (ctx->watchers) {
            struct watch_ctx *w;

            while ((w = zlist_pop (ctx->watchers))) {
                if (flux_kvs_lookup_cancel (w->f) < 0)
                    flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                                    __FUNCTION__);

                if (flux_respond_error (ctx->h, w->msg, ENOSYS, NULL) < 0)
                    flux_log_error (ctx->h, "%s: flux_respond_error",
                                    __FUNCTION__);
                watch_ctx_destroy (w);
            }
            zlist_destroy (&ctx->watchers);
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
    if (!(ctx->watchers = zlist_new ()))
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
