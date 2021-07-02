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

#include "src/common/libutil/errno_safe.h"

#include "ping.h"

struct ping_context {
    flux_msg_handler_t *mh;
    char *uuid;
};

static char *make_json_response_payload (const char *request_payload,
                                         const char *route,
                                         struct flux_msg_cred cred)
{
    json_t *o = NULL;
    json_t *add = NULL;
    char *result = NULL;

    if (!request_payload || !(o = json_loads (request_payload, 0, NULL))) {
        errno = EPROTO;
        goto done;
    }
    if (!(add = json_pack ("{s:s s:i s:i}", "route", route,
                                            "userid", cred.userid,
                                            "rolemask", cred.rolemask))) {
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
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, add);
    return result;
}

static void ping_request_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    struct ping_context *p = arg;
    const char *json_str;
    char *route_str = NULL;
    char *new_str;
    size_t new_size;
    char *resp_str = NULL;
    struct flux_msg_cred cred;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (flux_msg_get_cred (msg, &cred) < 0)
        goto error;

    /* The route string as obtained from the message includes all
     * hops but the last one, e.g. the identity of the destination.
     * That identity is passed in to ping_initialize() as the uuid.
     * Tack it onto the route string.
     */
    if (!(route_str = flux_msg_route_string (msg)))
        goto error;
    new_size = strlen (route_str) + strlen (p->uuid) + 2;
    if (!(new_str = realloc (route_str, new_size)))
        goto error;
    route_str = new_str;
    strcat (route_str, "!");
    strcat (route_str, p->uuid);

    if (!(resp_str = make_json_response_payload (json_str, route_str, cred)))
        goto error;
    if (flux_respond (h, msg, resp_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (route_str);
    free (resp_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (route_str);
    free (resp_str);
}

static void ping_finalize (void *arg)
{
    struct ping_context *p = arg;
    if (p) {
        int saved_errno = errno;
        flux_msg_handler_stop (p->mh);
        flux_msg_handler_destroy (p->mh);
        free (p->uuid);
        free (p);
        errno = saved_errno;
    }
}

int ping_initialize (flux_t *h, const char *service, const char *uuid)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct ping_context *p = calloc (1, sizeof (*p));
    if (!p)
        goto error;
    /* The uuid is tacked onto the route string constructed for
     * ping responses.  Truncate the uuid to 8 chars to match policy
     * of flux_msg_route_string().
     */
    if (!(p->uuid = strdup (uuid)))
        goto error;
    if (strlen (p->uuid) > 8)
        p->uuid[8] = '\0';

    match.typemask = FLUX_MSGTYPE_REQUEST;
    if (flux_match_asprintf (&match, "%s.ping", service) < 0)
        goto error;
    if (!(p->mh = flux_msg_handler_create (h, match, ping_request_cb, p)))
        goto error;
    flux_msg_handler_allow_rolemask (p->mh, FLUX_ROLE_ALL);
    flux_msg_handler_start (p->mh);
    flux_aux_set (h, "flux::ping", p, ping_finalize);
    flux_match_free (match);
    return 0;
error:
    flux_match_free (match);
    ping_finalize (p);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
