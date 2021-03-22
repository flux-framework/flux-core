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

/* Is request's cred authorized to disconnect/cancel 'msg'?
 */
static bool authorize (struct flux_msg_cred cred, const flux_msg_t *msg)
{
    uint32_t userid;

    if ((cred.rolemask & FLUX_ROLE_OWNER))
        return true;
    if ((cred.rolemask & FLUX_ROLE_USER)
        && flux_msg_get_userid (msg, &userid) == 0
        && cred.userid == userid)
        return true;
    return false;
}

int flux_msglist_disconnect (struct flux_msglist *l, const flux_msg_t *msg)
{
    struct flux_msg_cred cred;
    const flux_msg_t *item;
    int count = 0;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    item = flux_msglist_first (l);
    while (item) {
        if (flux_msg_match_route_first (msg, item) && authorize (cred, item)) {
            flux_msglist_delete (l);
            count++;
        }
        item = flux_msglist_next (l);
    }
    return count;
}

static bool match_matchtag (const flux_msg_t *msg, uint32_t matchtag)
{
    uint32_t tag;

    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return false;
    if (tag != matchtag)
        return false;
    return true;
}

int flux_msglist_cancel (flux_t *h,
                         struct flux_msglist *l,
                         const flux_msg_t *msg)
{
    struct flux_msg_cred cred;
    const flux_msg_t *item;
    uint32_t matchtag;
    int count = 0;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    item = flux_msglist_first (l);
    while (item) {
        if (match_matchtag (item, matchtag)
            && flux_msg_match_route_first (msg, item)
            && authorize (cred, item)) {
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

