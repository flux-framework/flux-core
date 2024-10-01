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
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

/* Parse the submit userid from the event log.
 * RFC 18 defines the structure of eventlogs.
 * RFC 21 requires that the first entry is "submit" and defines its context.
 */
static int eventlog_get_userid (struct info_ctx *ctx,
                                const char *s,
                                uint32_t *useridp)
{
    json_t *o = NULL;
    const char *name;
    int userid;
    int rv = -1;

    if (!s
        || !(o = json_loads (s, JSON_DISABLE_EOF_CHECK, NULL))
        || json_unpack (o,
                        "{s:s s:{s:i}}",
                        "name", &name,
                        "context",
                          "userid", &userid) < 0
        || !streq (name, "submit")) {
        errno = EPROTO;
        goto error;
    }
    (*useridp) = userid;
    rv = 0;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
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

int eventlog_allow (struct info_ctx *ctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    const char *s)
{
    uint32_t userid;
    if (eventlog_get_userid (ctx, s, &userid) < 0)
        return -1;
    store_lru (ctx, id, userid);
    if (flux_msg_authorize (msg, userid) < 0)
        return -1;
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
