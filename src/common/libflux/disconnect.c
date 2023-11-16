/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* disconnect.c - helpers for managing RFC 6 disconnect and cancel requests
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "ccan/str/str.h"

int flux_msg_match_init (struct flux_msg_match *match,
                         const flux_msg_t *msg)
{
    if (!match || !msg) {
        errno = EINVAL;
        return -1;
    }
    match->matchtag = FLUX_MATCHTAG_NONE;
    match->route_first = flux_msg_route_first (msg);
    if (flux_msg_has_payload (msg)
        && flux_msg_unpack (msg, "{s?i}", "matchtag", &match->matchtag) < 0)
        return -1;
    if (flux_msg_get_cred (msg, &match->cred) < 0)
        return -1;
    return 0;
}

bool flux_disconnect_match (const flux_msg_t *msg1, const flux_msg_t *msg2)
{
    struct flux_msg_cred cred;
    uint32_t userid;

    if (!flux_msg_route_match_first (msg1, msg2))
        return false;

    if (flux_msg_get_cred (msg1, &cred) < 0)
        return false;

    if (flux_msg_get_userid (msg2, &userid) < 0)
        return false;

    if (flux_msg_cred_authorize (cred, userid) < 0)
        return false;

    return true;
}

static bool msg_match_route_first (struct flux_msg_match *match,
                                   const flux_msg_t *msg)
{
    const char *route = flux_msg_route_first (msg);

    if (!route && !match->route_first)
        return true;
    if (route && match->route_first && streq (route, match->route_first))
        return true;
    return false;
}

bool flux_disconnect_match_ex (struct flux_msg_match *match,
                               const flux_msg_t *msg)
{
    uint32_t userid;

    if (!msg_match_route_first (match, msg))
        return false;

    if (flux_msg_get_userid (msg, &userid) < 0)
        return false;

    if (flux_msg_cred_authorize (match->cred, userid) < 0)
        return false;

    return true;
}

int flux_msglist_disconnect (struct flux_msglist *l, const flux_msg_t *msg)
{
    struct flux_msg_match match;
    const flux_msg_t *item;
    int count = 0;

    if (flux_msg_match_init (&match, msg) < 0)
        return -1;

    item = flux_msglist_first (l);
    while (item) {
        if (flux_disconnect_match_ex (&match, item)) {
            flux_msglist_delete (l);
            count++;
        }
        item = flux_msglist_next (l);
    }
    return count;
}

bool flux_cancel_match (const flux_msg_t *msg1, const flux_msg_t *msg2)
{
    uint32_t matchtag;
    uint32_t tag;

    if (!flux_disconnect_match (msg1, msg2))
        return false;

    if (flux_request_unpack (msg1, NULL, "{s:i}", "matchtag", &matchtag) < 0)
        return false;

    if (flux_msg_get_matchtag (msg2, &tag) < 0)
        return false;

    if (tag != matchtag)
        return false;

    return true;
}

bool flux_cancel_match_ex (struct flux_msg_match *match,
                           const flux_msg_t *msg)
{
    uint32_t tag;

    if (!flux_disconnect_match_ex (match, msg))
        return false;

    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return false;

    if (tag != match->matchtag)
        return false;

    return true;
}

int flux_msglist_cancel (flux_t *h,
                         struct flux_msglist *l,
                         const flux_msg_t *msg)
{
    struct flux_msg_match match;
    const flux_msg_t *item;
    int count = 0;

    if (flux_msg_match_init (&match, msg) < 0)
        return -1;

    item = flux_msglist_first (l);
    while (item) {
        if (flux_cancel_match_ex (&match, item)) {
            if (flux_respond_error (h, item, ENODATA, NULL) < 0)
                return -1;
            flux_msglist_delete (l);
            count++;
            break;
        }
        item = flux_msglist_next (l);
    }
    return count;
}

/*
 * vi:ts=4 sw=4 expandtab
 */

