/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* lookup.c - lookup in job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "job-info.h"
#include "lookup.h"
#include "update.h"
#include "allow.h"
#include "util.h"

struct lookup_ctx {
    struct info_ctx *ctx;
    const flux_msg_t *msg;
    flux_jobid_t id;
    json_t *keys;
    bool lookup_eventlog;
    int flags;
    flux_future_t *f;
    bool allow;
};

static void info_lookup_continuation (flux_future_t *fall, void *arg);

static void lookup_ctx_destroy (void *data)
{
    struct lookup_ctx *ctx = data;

    if (ctx) {
        int saved_errno = errno;
        flux_msg_decref (ctx->msg);
        json_decref (ctx->keys);
        flux_future_destroy (ctx->f);
        free (ctx);
        errno = saved_errno;
    }
}

static struct lookup_ctx *lookup_ctx_create (struct info_ctx *ctx,
                                             const flux_msg_t *msg,
                                             flux_jobid_t id,
                                             json_t *keys,
                                             int flags)
{
    struct lookup_ctx *l = calloc (1, sizeof (*l));

    if (!l)
        return NULL;

    l->ctx = ctx;
    l->id = id;
    l->flags = flags;

    if (!(l->keys = json_copy (keys))) {
        errno = ENOMEM;
        goto error;
    }

    l->msg = flux_msg_incref (msg);

    return l;

error:
    lookup_ctx_destroy (l);
    return NULL;
}

static int lookup_key (struct lookup_ctx *l,
                       flux_future_t *fall,
                       const char *key)
{
    flux_future_t *f = NULL;
    char path[64];

    /* Check for duplicate key, return if already looked up */
    if (flux_future_get_child (fall, key) != NULL)
        return 0;

    if (flux_job_kvs_key (path, sizeof (path), l->id, key) < 0
        || !(f = flux_kvs_lookup (l->ctx->h, NULL, 0, path))
        || flux_future_push (fall, key, f) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    return 0;
}

static int lookup_keys (struct lookup_ctx *l)
{
    flux_future_t *fall = NULL;
    size_t index;
    json_t *key;

    if (!(fall = flux_future_wait_all_create ()))
        return -1;
    flux_future_set_flux (fall, l->ctx->h);

    if (l->lookup_eventlog) {
        if (lookup_key (l, fall, "eventlog") < 0)
            goto error;
    }

    json_array_foreach (l->keys, index, key) {
        if (lookup_key (l, fall, json_string_value (key)) < 0)
            goto error;
    }

    if (flux_future_then (fall, -1, info_lookup_continuation, l) < 0)
        goto error;

    l->f = fall;
    return 0;

error:
    flux_future_destroy (fall);
    return -1;
}

static int lookup_current (struct lookup_ctx *l,
                           flux_future_t *fall,
                           const char *key,
                           const char *value,
                           char **current_value)
{
    flux_future_t *f_eventlog;
    const char *s_eventlog;
    json_t *value_object = NULL;
    json_t *eventlog = NULL;
    size_t index;
    json_t *entry;
    const char *update_event_name = NULL;
    char *value_object_str = NULL;
    int save_errno;

    if (streq (key, "R"))
        update_event_name = "resource-update";
    else if (streq (key, "jobspec"))
        update_event_name = "jobspec-update";

    if (!(value_object = json_loads (value, 0, NULL))) {
        errno = EINVAL;
        goto error;
    }

    if (!(f_eventlog = flux_future_get_child (fall, "eventlog"))) {
        flux_log_error (l->ctx->h,
                        "%s: flux_future_get_child",
                        __FUNCTION__);
        goto error;
    }

    if (flux_kvs_lookup_get (f_eventlog, &s_eventlog) < 0) {
        if (errno != ENOENT) {
            flux_log_error (l->ctx->h,
                            "%s: flux_kvs_lookup_get",
                            __FUNCTION__);
        }
        goto error;
    }

    if (!(eventlog = eventlog_decode (s_eventlog))) {
        errno = EINVAL;
        goto error;
    }

    json_array_foreach (eventlog, index, entry) {
        const char *name;
        json_t *context = NULL;
        if (eventlog_entry_parse (entry, NULL, &name, &context) < 0)
            goto error;
        if (streq (name, update_event_name)) {
            if (streq (key, "R"))
                apply_updates_R (l->ctx->h, l->id, key, value_object, context);
            else if (streq (key, "jobspec"))
                apply_updates_jobspec (l->ctx->h,
                                       l->id,
                                       key,
                                       value_object,
                                       context);
        }
    }

    if (!(value_object_str = json_dumps (value_object, 0)))
        goto error;

    (*current_value) = value_object_str;
    json_decref (eventlog);
    json_decref (value_object);
    return 0;

error:
    save_errno = errno;
    json_decref (eventlog);
    json_decref (value_object);
    free (value_object_str);
    errno = save_errno;
    return -1;
}

static void info_lookup_continuation (flux_future_t *fall, void *arg)
{
    struct lookup_ctx *l = arg;
    struct info_ctx *ctx = l->ctx;
    const char *s;
    char *current_value = NULL;
    size_t index;
    json_t *key;
    json_t *o = NULL;
    json_t *tmp = NULL;
    char *data = NULL;
    flux_error_t error;

    if (!l->allow) {
        flux_future_t *f;

        if (!(f = flux_future_get_child (fall, "eventlog"))) {
            errprintf (&error,
                       "internal error: flux_future_get_child eventlog: %s",
                       strerror (errno));
            goto error;
        }

        if (flux_kvs_lookup_get (f, &s) < 0) {
            errprintf (&error,
                       "%s",
                       errno == ENOENT ? "invalid job id" : strerror (errno));
            goto error;
        }

        if (eventlog_allow (ctx, l->msg, l->id, s) < 0) {
            char *errmsg;
            if (errno == EPERM)
                errmsg = "access is restricted to job/instance owner";
            else
                errmsg = "error parsing eventlog";
            errprintf (&error, "%s", errmsg);
            goto error;
        }
        l->allow = true;
    }

    if (!(o = json_object ())
        || !(tmp = json_integer (l->id))
        || json_object_set_new (o, "id", tmp) < 0) {
        errprintf (&error, "error creating response object");
        json_decref (tmp);
        goto enomem;
    }

    json_array_foreach (l->keys, index, key) {
        flux_future_t *f;
        const char *keystr = json_string_value (key); /* validated earlier */
        json_t *val = NULL;

        if (!(f = flux_future_get_child (fall, keystr))) {
            errprintf (&error,
                       "internal error: flux_future_get_child %s: %s",
                       keystr,
                       strerror (errno));
            goto error;
        }

        if (flux_kvs_lookup_get (f, &s) < 0) {
            errprintf (&error,
                       "%s: %s",
                       keystr,
                       errno == ENOENT ? "key not found" : strerror (errno));
            goto error;
        }

        /* treat empty value as invalid */
        if (!s) {
            errprintf (&error, "%s: value is unexpectedly empty", keystr);
            errno = EPROTO;
            goto error;
        }

        if ((l->flags & FLUX_JOB_LOOKUP_CURRENT)
            && (streq (keystr, "R") || streq (keystr, "jobspec"))) {
            if (lookup_current (l, fall, keystr, s, &current_value) < 0) {
                errprintf (&error,
                           "%s: error applying eventlog to original value: %s",
                           keystr,
                           strerror (errno));
                goto error;
            }
            s = current_value;
        }

        /* check for JSON_DECODE flag last, as changes above could affect
         * desired value */
        if ((l->flags & FLUX_JOB_LOOKUP_JSON_DECODE)
            && (streq (keystr, "jobspec") || streq (keystr, "R"))) {
            /* We assume if it was stored in the KVS it's valid JSON,
             * so failure is ENOMEM */
            val = json_loads (s, 0, NULL);
        }
        else
            val = json_string (s);
        if (!val || json_object_set_new (o, keystr, val) < 0) {
            json_decref (val);
            errprintf (&error, "%s: error adding value to response", keystr);
            goto enomem;
        }

        free (current_value);
        current_value = NULL;
    }

    /* must have been allowed earlier or above, otherwise should have
     * taken error path */
    assert (l->allow);

    if (!(data = json_dumps (o, JSON_COMPACT))) {
        errprintf (&error, "error preparing response");
        goto enomem;
    }
    if (flux_respond (ctx->h, l->msg, data) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);

    goto done;

enomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (ctx->h, l->msg, errno, error.text) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

done:
    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    json_decref (o);
    free (data);
    free (current_value);
    zlist_remove (ctx->lookups, l);
}

/* Check if lookup allowed, either b/c message is from instance owner
 * or previous lookup verified it's ok.
 */
static int check_allow (struct lookup_ctx *l)
{
    int ret;

    /* if rpc from owner, no need to do guest access check */
    if (flux_msg_authorize (l->msg, FLUX_USERID_UNKNOWN) == 0) {
        l->allow = true;
        return 0;
    }

    if ((ret = eventlog_allow_lru (l->ctx,
                                   l->msg,
                                   l->id)) < 0)
        return -1;

    if (ret) {
        l->allow = true;
        return 0;
    }

    return 0;
}

/* If we need the eventlog for an allow check or for update-lookup
 * we need to add it to the key lookup list.
 */
static void check_to_lookup_eventlog (struct lookup_ctx *l)
{
    if (!l->allow || (l->flags & FLUX_JOB_LOOKUP_CURRENT)) {
        size_t index;
        json_t *key;
        json_array_foreach (l->keys, index, key) {
            if (streq (json_string_value (key), "eventlog"))
                return;
        }
        l->lookup_eventlog = true;
    }
}

static json_t *get_json_string (json_t *o)
{
    char *s = json_dumps (o, JSON_ENCODE_ANY);
    json_t *tmp = NULL;
    /* We assume json is internally valid, thus this is an ENOMEM error */
    if (!s) {
        errno = ENOMEM;
        goto cleanup;
    }
    if (!(tmp = json_string (s))) {
        errno = ENOMEM;
        goto cleanup;
    }
cleanup:
    free (s);
    return tmp;
}

/* returns -1 on error, 1 on cached response returned, 0 on no cache */
static int lookup_cached (struct lookup_ctx *l)
{
    json_t *current_object = NULL;
    json_t *key;
    const char *key_str;
    int ret, rv = -1;

    /* Special optimization, looking for a single updated value that
     * could be cached via an update-watch
     *
     * - Caller must want current / updated value
     * - This lookup is already allowed (i.e. if we have to do a
     *   "allow" KVS lookup, there is little benefit to returning the
     *   cached value).
     * - The caller only wants one key (i.e. if we have to do lookup
     *   on another value anyways, there is little benefit to
     *   returning the cached value).
     */

    if (!(l->flags & FLUX_JOB_LOOKUP_CURRENT)
        || !l->allow
        || json_array_size (l->keys) != 1)
        return 0;

    key = json_array_get (l->keys, 0);
    if (!key) {
        errno = EINVAL;
        goto cleanup;
    }

    key_str = json_string_value (key);

    if (!streq (key_str, "R") && !streq (key_str, "jobspec"))
        return 0;

    if ((ret = update_watch_get_cached (l->ctx,
                                        l->id,
                                        key_str,
                                        &current_object)) < 0)
        goto cleanup;

    if (ret) {
        if (l->flags & FLUX_JOB_LOOKUP_JSON_DECODE) {
            if (flux_respond_pack (l->ctx->h,
                                   l->msg,
                                   "{s:I s:O}",
                                   "id", l->id,
                                   key_str, current_object) < 0) {
                flux_log_error (l->ctx->h, "%s: flux_respond", __FUNCTION__);
                goto cleanup;
            }
            rv = 1;
            goto cleanup;
        }
        else {
            json_t *o = get_json_string (current_object);
            if (!o) {
                errno = ENOMEM;
                goto cleanup;
            }
            if (flux_respond_pack (l->ctx->h,
                                   l->msg,
                                   "{s:I s:O}",
                                   "id", l->id,
                                   key_str, o) < 0) {
                json_decref (o);
                flux_log_error (l->ctx->h, "%s: flux_respond", __FUNCTION__);
                goto cleanup;
            }
            rv = 1;
            json_decref (o);
            goto cleanup;
        }
    }

    rv = 0;
cleanup:
    json_decref (current_object);
    return rv;
}

static int lookup (flux_t *h,
                   const flux_msg_t *msg,
                   struct info_ctx *ctx,
                   flux_jobid_t id,
                   json_t *keys,
                   int flags,
                   flux_error_t *error)
{
    struct lookup_ctx *l = NULL;
    int ret;

    if (!(l = lookup_ctx_create (ctx, msg, id, keys, flags))) {
        errprintf (error,
                   "could not create lookup context: %s",
                   strerror (errno));
        goto error;
    }

    if (check_allow (l) < 0) {
        errprintf (error, "access is restricted to job/instance owner");
        goto error;
    }

    if ((ret = lookup_cached (l)) < 0) {
        errprintf (error,
                   "internal error attempting to use update-watch cache: %s",
                   strerror (errno));
        goto error;
    }

    if (ret) {
        lookup_ctx_destroy (l);
        return 0;
    }

    check_to_lookup_eventlog (l);

    if (lookup_keys (l) < 0) {
        errprintf (error,
                   "error sending KVS lookup request(s): %s",
                   strerror (errno));
        goto error;
    }

    if (zlist_append (ctx->lookups, l) < 0) {
        errprintf (error,
                   "internal error saving lookup context: out of memory");
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (ctx->lookups, l, lookup_ctx_destroy, true);
    return 0;

error:
    lookup_ctx_destroy (l);
    return -1;
}

void lookup_cb (flux_t *h,
                flux_msg_handler_t *mh,
                const flux_msg_t *msg,
                void *arg)
{
    struct info_ctx *ctx = arg;
    size_t index;
    json_t *key;
    json_t *keys;
    flux_jobid_t id;
    int flags;
    int valid_flags = FLUX_JOB_LOOKUP_JSON_DECODE | FLUX_JOB_LOOKUP_CURRENT;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:o s:i}",
                             "id", &id,
                             "keys", &keys,
                             "flags", &flags) < 0)
        goto error;

    if (flags & ~valid_flags) {
        errno = EPROTO;
        errmsg = "lookup request rejected with invalid flag";
        goto error;
    }

    /* validate keys is an array and all fields are strings */
    if (!json_is_array (keys)) {
        errno = EPROTO;
        goto error;
    }

    json_array_foreach (keys, index, key) {
        if (!json_is_string (key)) {
            errno = EPROTO;
            goto error;
        }
    }

    if (lookup (h, msg, ctx, id, keys, flags, &error) < 0) {
        errmsg = error.text;
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* legacy rpc target */
void update_lookup_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct info_ctx *ctx = arg;
    flux_jobid_t id;
    const char *key = NULL;
    json_t *keys = NULL;
    int flags;
    int valid_flags = 0;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:s s:i}",
                             "id", &id,
                             "key", &key,
                             "flags", &flags) < 0)
        goto error;
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

    if (!(keys = json_pack ("[s]", key))) {
        errno = ENOMEM;
        goto error;
    }

    if (lookup (h,
                msg,
                ctx,
                id,
                keys,
                FLUX_JOB_LOOKUP_JSON_DECODE | FLUX_JOB_LOOKUP_CURRENT,
                &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (keys);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
