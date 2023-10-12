/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <string.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

flux_msg_t *create_request (int sender,
                            uint32_t rolemask,
                            uint32_t userid,
                            uint32_t matchtag)
{
    char id[32];
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("foo", NULL)))
        return NULL;
    snprintf (id, sizeof (id), "%d", sender);
    if (flux_msg_route_push (msg, id) < 0)
        return NULL;
    if (flux_msg_set_rolemask (msg, rolemask) < 0)
        return NULL;
    if (flux_msg_set_userid (msg, userid) < 0)
        return NULL;
    if (flux_msg_set_matchtag (msg, matchtag) < 0)
        return NULL;
    return msg;
}

flux_msg_t *create_cancel (int sender,
                           uint32_t rolemask,
                           uint32_t userid,
                           uint32_t matchtag)
{
    char id[32];
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("foo", NULL)))
        return NULL;
    snprintf (id, sizeof (id), "%d", sender);
    if (flux_msg_route_push (msg, id) < 0)
        return NULL;
    if (flux_msg_set_rolemask (msg, rolemask) < 0)
        return NULL;
    if (flux_msg_set_userid (msg, userid) < 0)
        return NULL;
    if (flux_msg_pack (msg, "{s:i}", "matchtag", matchtag) < 0)
        return NULL;
    return msg;
}

void check_disconnect (void)
{
    struct flux_msglist *l;
    int i;
    flux_msg_t *msg;
    int count;

    /* populate list of requests with unique senders
     */
    if (!(l = flux_msglist_create ()))
        BAIL_OUT ("flux_msglist_create failed");
    for (i = 0; i < 8; i++) {
        if (!(msg = create_request (i, 0, i, 0)))
            BAIL_OUT ("could not create test message");
        if (flux_msglist_append (l, msg) < 0)
            BAIL_OUT ("flux_msglist_append failed");
        flux_msg_decref (msg);
    }
    ok (flux_msglist_count (l) == 8,
        "msglist contains 8 messages");

    /* disconnect the first four requests
     * The disconnect request will be sent by the same sender and user.
     */
    for (i = 0; i < 4; i++) {
        if (!(msg = create_request (i, FLUX_ROLE_USER, i, 0)))
            BAIL_OUT ("could not create disconnect message");
        count = flux_msglist_disconnect (l, msg);
        ok (count == 1,
            "flux_msglist_disconnect removed message");
        flux_msg_decref (msg);
    }
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* sender doesn't match
     */
    if (!(msg = create_request (42, FLUX_ROLE_USER, 4, 0)))
        BAIL_OUT ("could not create disconnect message");
    count = flux_msglist_disconnect (l, msg);
    ok (count == 0,
        "flux_msglist_disconnect with unknown sender has no effect");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* FLUX_ROLE_USER and non-matching userid
     */
    if (!(msg = create_request (4, FLUX_ROLE_USER, 5, 0)))
        BAIL_OUT ("could not create disconnect message");
    count = flux_msglist_disconnect (l, msg);
    ok (count == 0,
        "flux_msglist_disconnect (user) with wrong userid has no effect");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* FLUX_ROLE_OWNER and non-matching userid
     */
    if (!(msg = create_request (4, FLUX_ROLE_OWNER, 5, 0)))
        BAIL_OUT ("could not create disconnect message");
    count = flux_msglist_disconnect (l, msg);
    ok (count == 1,
        "flux_msglist_disconnect (owner) with wrong userid removed message");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 3,
        "msglist contains 3 messages");

    flux_msglist_destroy (l);
}

void check_cancel (void)
{
    flux_t *h;
    struct flux_msglist *l;
    int i;
    flux_msg_t *msg;
    int count;
    uint32_t matchtag;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("failed to create loop handle");

    /* populate list of requests with unique senders
     */
    if (!(l = flux_msglist_create ()))
        BAIL_OUT ("flux_msglist_create failed");
    for (i = 1; i < 8; i++) {
        if (!(msg = create_request (i, 0, i, i)))
            BAIL_OUT ("could not create test message");
        if (flux_msglist_append (l, msg) < 0)
            BAIL_OUT ("flux_msglist_append failed");
        flux_msg_decref (msg);
    }
    ok (flux_msglist_count (l) == 7,
        "msglist contains 7 messages");

    /* cancel the first three requests
     */
    int failures = 0;
    for (i = 1; i < 4; i++) {
        if (!(msg = create_cancel (i, FLUX_ROLE_USER, i, i)))
            BAIL_OUT ("could not create cancel message");
        count = flux_msglist_cancel (h, l, msg);
        flux_msg_decref (msg);
        if (count != 1) {
            failures++;
            continue;
        }
        if (!(msg = flux_recv (h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
            failures++;
            continue;
        }
        if (flux_msg_get_matchtag (msg, &matchtag) < 0 || i != matchtag)
            failures++;
        flux_msg_decref (msg);
    }
    ok (failures == 0,
        "flux_msglist_cancel canceled 3 messages");
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* sender doesn't match
     */
    if (!(msg = create_cancel (42, FLUX_ROLE_USER, 4, 4)))
        BAIL_OUT ("could not create cancel message");
    count = flux_msglist_cancel (h, l, msg);
    ok (count == 0,
        "flux_msglist_cancel with unknown sender has no effect");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* FLUX_ROLE_USER and non-matching userid
     */
    if (!(msg = create_cancel (4, FLUX_ROLE_USER, 5, 4)))
        BAIL_OUT ("could not create cancel message");
    count = flux_msglist_cancel (h, l, msg);
    ok (count == 0,
        "flux_msglist_cancel (user) with wrong userid has no effect");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 4,
        "msglist contains 4 messages");

    /* FLUX_ROLE_OWNER and non-matching userid
     */
    if (!(msg = create_cancel (6, FLUX_ROLE_OWNER, 5, 6)))
        BAIL_OUT ("could not create cancel message");
    count = flux_msglist_cancel (h, l, msg);
    ok (count == 1,
        "flux_msglist_cancel (owner) with wrong userid removed message");
    flux_msg_decref (msg);
    ok (flux_msglist_count (l) == 3,
        "msglist contains 3 messages");
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK)) != NULL
       && flux_msg_get_matchtag (msg, &matchtag) == 0
       && matchtag == 6,
       "flux_msglist_cancel responded to message");
    flux_msg_destroy (msg);

    flux_msglist_destroy (l);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_disconnect ();
    check_cancel ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

