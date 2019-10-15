/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_AUTH_H
#define _ROUTER_AUTH_H

#include <flux/core.h>

struct auth_cred {
    uint32_t userid;
    uint32_t rolemask;
};

/* Look up user in the 'userdb' to determine assigned roles.
 */
flux_future_t *auth_lookup_rolemask (flux_t *h, uint32_t userid);
int auth_lookup_rolemask_get (flux_future_t *f, uint32_t *rolemask);

/* Initialize received message creds based on the connected user's credentials.
 */
int auth_init_message (flux_msg_t *msg, const struct auth_cred *conn);

/* Determine whether event 'msg' may be received by a connection,
 * based on connected user's credentials.
 */
int auth_check_event_privacy (const flux_msg_t *msg,
                              const struct auth_cred *conn);

#endif /* !_ROUTER_AUTH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
