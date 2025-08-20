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
#include <poll.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"


void check_msglist (void)
{
    struct flux_msglist *l;
    flux_msg_t *msg1;
    flux_msg_t *msg2;

    if (!(msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    ok (flux_msglist_count (NULL) == 0,
        "flux_msglist_count l=NULL is 0");
    l = flux_msglist_create ();
    ok (l != NULL,
        "flux_msglist_create works");
    ok (flux_msglist_count (l) == 0,
        "flux_msglist_count is 0");

    ok (flux_msglist_append (l, msg1) == 0,
        "flux_msglist_append msg1 works");
    ok (flux_msglist_count (l) == 1,
        "flux_msglist_count is 1");
    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_first is msg1");
    ok (flux_msglist_last (l) == msg1,
        "flux_msglist_last is msg1");
    ok (flux_msglist_next (l) == NULL,
        "flux_msglist_next is NULL");

    ok (flux_msglist_append (l, msg2) == 0,
        "flux_msglist_append msg2 works");
    ok (flux_msglist_count (l) == 2,
        "flux_msglist_count is 2");
    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_first is msg1");
    ok (flux_msglist_next (l) == msg2,
        "flux_msglist_next is msg2");
    ok (flux_msglist_last (l) == msg2,
        "flux_msglist_last is msg2");

    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_last is msg1 (assigning curosr to msg1)");
    flux_msglist_delete (l);
    ok (flux_msglist_count (l) == 1,
        "flux_msglist_count is 1 after delete");
    ok (flux_msglist_first (l) == msg2,
        "flux_msglist_first is now msg2");

    flux_msg_decref (msg1);
    flux_msg_decref (msg2);

    flux_msglist_destroy (l);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_msglist ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
