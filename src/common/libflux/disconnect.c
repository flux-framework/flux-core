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

int flux_msglist_disconnect (struct flux_msglist *l, const flux_msg_t *msg)
{
    const flux_msg_t *item;
    int count = 0;

    item = flux_msglist_first (l);
    while (item) {
        if (flux_disconnect_match (msg, item)) {
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

int flux_msglist_cancel (flux_t *h,
                         struct flux_msglist *l,
                         const flux_msg_t *msg)
{
    const flux_msg_t *item;
    int count = 0;

    item = flux_msglist_first (l);
    while (item) {
        if (flux_cancel_match (msg, item)) {
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

