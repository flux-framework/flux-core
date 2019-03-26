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

#include "info.h"
#include "allow.h"
#include "util.h"

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
    free (event);
    json_decref (o);
    return -1;
}

int eventlog_allow (struct info_ctx *ctx, const flux_msg_t *msg,
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
