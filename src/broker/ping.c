/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>
#include "ping.h"

struct ping_context {
    flux_msg_handler_t *mh;
};

static char *make_json_response_payload (const char *request_payload,
                                         const char *route,
                                         uint32_t userid,
                                         uint32_t rolemask)
{
    json_t *o = NULL;
    json_t *add = NULL;
    char *result = NULL;

    if (!request_payload || !(o = json_loads (request_payload, 0, NULL))) {
        errno = EPROTO;
        goto done;
    }
    if (!(add = json_pack ("{s:s s:i s:i}",
                           "route",
                           route,
                           "userid",
                           userid,
                           "rolemask",
                           rolemask))) {
        errno = ENOMEM;
        goto done;
    }
    if (json_object_update (o, add) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (!(result = json_dumps (o, 0))) {
        errno = ENOMEM;
        goto done;
    }
done:
    json_decref (o);
    json_decref (add);
    return result;
}

static void ping_request_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    const char *json_str;
    char *route_str = NULL;
    char *full_route_str = NULL;
    char *resp_str = NULL;
    uint32_t rank, userid, rolemask;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (flux_msg_get_userid (msg, &userid) < 0)
        goto error;
    if (!(route_str = flux_msg_get_route_string (msg)))
        goto error;
    if (flux_get_rank (h, &rank) < 0)
        goto error;
    if (asprintf (&full_route_str, "%s!%u", route_str, rank) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!(resp_str = make_json_response_payload (json_str,
                                                 full_route_str,
                                                 userid,
                                                 rolemask))) {
        goto error;
    }
    if (flux_respond (h, msg, resp_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (route_str);
    free (full_route_str);
    free (resp_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (route_str);
    free (full_route_str);
    free (resp_str);
}

static void ping_finalize (void *arg)
{
    struct ping_context *p = arg;
    flux_msg_handler_stop (p->mh);
    flux_msg_handler_destroy (p->mh);
    free (p);
}

int ping_initialize (flux_t *h, const char *service)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct ping_context *p = calloc (1, sizeof (*p));
    if (!p) {
        errno = ENOMEM;
        goto error;
    }
    match.typemask = FLUX_MSGTYPE_REQUEST;
    if (asprintf (&match.topic_glob, "%s.ping", service) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!(p->mh = flux_msg_handler_create (h, match, ping_request_cb, p)))
        goto error;
    flux_msg_handler_allow_rolemask (p->mh, FLUX_ROLE_ALL);
    flux_msg_handler_start (p->mh);
    flux_aux_set (h, "flux::ping", p, ping_finalize);
    free (match.topic_glob);
    return 0;
error:
    free (match.topic_glob);
    if (p)
        ping_finalize (p);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
