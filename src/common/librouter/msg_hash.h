/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_MSG_HASH_H
#define _ROUTER_MSG_HASH_H

#include "src/common/libczmqcontainers/czmq_containers.h"

typedef enum {
    MSG_HASH_TYPE_UUID_MATCHTAG = 1,    // Hash request/response messages based
                                        // on sender uuid+matchtag such that
                                        // request and its response have the
                                        // same hash key.

} msg_hash_type_t;

/* Create a zhashx_t for Flux messages.  The hash key is derived from
 * info in the message, with key hasher and key comparator methods set up
 * as appropriate for the hash type chosen at creation.
 *
 * The key duplicator and destructor are disabled, since the message contains
 * all of the key information.
 *
 * The entry duplicator and destructor are set to flux_msg_incref() and
 * flux_msg_decref() respectively.
 */
zhashx_t *msg_hash_create (msg_hash_type_t hashtype);

#endif /* _ROUTER_MSG_HASH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

