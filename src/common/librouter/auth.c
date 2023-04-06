/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>
#include "auth.h"

int auth_init_message (flux_msg_t *msg, const struct flux_msg_cred *conn)
{
    if (!msg || !conn) {
        errno = EINVAL;
        return -1;
    }
    /* Guest:
     * Unconditionally overwrite message credentials with connect creds.
     */
    if (!(conn->rolemask & FLUX_ROLE_OWNER)) {
        if (flux_msg_set_userid (msg, conn->userid) < 0)
            return -1;
        if (flux_msg_set_rolemask (msg, conn->rolemask) < 0)
            return -1;
    }
    /* Owner:
     * If message credentials have been set, we allow them to pass through.
     * Use case #1: owner message router components, where auth is "downstream"
     * Use case #2: testing, to simulate guest access.
     * If they have not been set, overwrite with connect creds, as above.
     */
    else {
        struct flux_msg_cred cred;

        if (flux_msg_get_userid (msg, &cred.userid) < 0)
            return -1;
        if (cred.userid == FLUX_USERID_UNKNOWN) {
            if (flux_msg_set_userid (msg, conn->userid) < 0)
                return -1;
        }

        if (flux_msg_get_rolemask (msg, &cred.rolemask) < 0)
            return -1;
        if (cred.rolemask == FLUX_ROLE_NONE) {
            if (flux_msg_set_rolemask (msg, conn->rolemask) < 0)
                return -1;
        }
    }
    return 0;
}

int auth_check_event_privacy (const flux_msg_t *msg,
                              const struct flux_msg_cred *cred)
{
    if (!msg || !cred) {
        errno = EINVAL;
        return -1;
    }
    /* Guest:
     * Event message may be received by client if privacy flag is not set
     * or connect userid matches message userid.
     */
    if (!(cred->rolemask & FLUX_ROLE_OWNER)) {
        if (flux_msg_is_private (msg)) {
            uint32_t userid;

            if (flux_msg_get_userid (msg, &userid) < 0)
                return -1;
            if (cred->userid != userid) {
                errno = EPERM;
                return -1;
            }
        }
    }
    /* Owner:
     * Event message may be unconditionally received by client.
     */
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
