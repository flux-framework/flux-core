/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* allow.c - handle eventlog access checks */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "job-info.h"
#include "allow.h"

#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

/* Parse the submit userid from the event log.
 * Assume "submit" is the first event.
 */
static int eventlog_get_userid (struct info_ctx *ctx,
                                const char *s,
                                uint32_t *useridp)
{
    json_t *a = NULL;
    json_t *entry = NULL;
    const char *name = NULL;
    json_t *context = NULL;
    int userid;
    int rv = -1;

    if (!(a = eventlog_decode (s))) {
        flux_log_error (ctx->h, "%s: eventlog_decode", __FUNCTION__);
        /* if eventlog improperly formatted, we'll consider this a
         * protocol error */
        if (errno == EINVAL)
            errno = EPROTO;
        goto error;
    }
    if (!(entry = json_array_get (a, 0))) {
        errno = EPROTO;
        goto error;
    }
    if (eventlog_entry_parse (entry, NULL, &name, &context) < 0) {
        flux_log_error (ctx->h, "%s: eventlog_decode", __FUNCTION__);
        goto error;
    }
    if (!streq (name, "submit") || !context) {
        flux_log (ctx->h, LOG_ERR, "%s: invalid event: %s", __FUNCTION__, name);
        errno = EPROTO;
        goto error;
    }
    if (json_unpack (context, "{ s:i }", "userid", &userid) < 0) {
        errno = EPROTO;
        goto error;
    }
    (*useridp) = userid;
    rv = 0;
error:
    json_decref (a);
    return rv;
}

static void store_lru (struct info_ctx *ctx, flux_jobid_t id, uint32_t userid)
{
    char key[64];
    uint32_t *userid_ptr = NULL;

    snprintf (key, 64, "%ju", (uintmax_t)id);

    if (!(userid_ptr = calloc (1, sizeof (userid))))
        return;
    (*userid_ptr) = userid;

    if (lru_cache_put (ctx->owner_lru, key, userid_ptr) < 0) {
        if (errno != EEXIST)
            flux_log_error (ctx->h, "%s: lru_cache_put", __FUNCTION__);
        free (userid_ptr);
        return;
    }
    return;
}

/* Optimization:
 * Avoid calling eventlog_get_userid() if message cred has OWNER role.
 */
int eventlog_allow (struct info_ctx *ctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    const char *s)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    if (!(cred.rolemask & FLUX_ROLE_OWNER)) {
        uint32_t userid;
        /* RFC18: empty eventlog not allowed */
        if (!s) {
            errno = EPROTO;
            return -1;
        }
        if (eventlog_get_userid (ctx, s, &userid) < 0)
            return -1;
        store_lru (ctx, id, userid);
        if (flux_msg_cred_authorize (cred, userid) < 0)
            return -1;
    }
    return 0;
}

int eventlog_allow_lru (struct info_ctx *ctx,
                        const flux_msg_t *msg,
                        flux_jobid_t id)
{
    char key[64];
    uint32_t *userid_ptr;

    snprintf (key, 64, "%ju", (uintmax_t)id);

    if ((userid_ptr = lru_cache_get (ctx->owner_lru, key))) {
        if (flux_msg_authorize (msg, (*userid_ptr)) < 0)
            return -1;
        return 1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
