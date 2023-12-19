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
#include <uuid.h>
#include <flux/core.h>

#include "src/common/librouter/rpc_track.h"
#include "src/common/libtap/tap.h"

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librouter/msg_hash.h"
#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

/* Create a disconnect with same uuid as 'req'.
 */
flux_msg_t *create_disconnect (const flux_msg_t *req)
{
    const char *topic;
    flux_msg_t *dis;
    char buf[128];

    if (!(dis = flux_msg_copy (req, false))
        || flux_msg_get_topic (req, &topic) < 0
        || snprintf (buf, sizeof (buf), "%s.disconnect", topic) >= sizeof (buf)
        || flux_msg_set_topic (dis, buf) < 0
        || flux_msg_set_noresponse (dis) < 0
        || flux_msg_set_matchtag (dis, FLUX_MATCHTAG_NONE) < 0)
        BAIL_OUT ("failed to create disconnect request");
    return dis;
}

flux_msg_t *create_response (const flux_msg_t *req, int errnum)
{
    flux_msg_t *rep;

    if (!(rep = flux_response_derive (req, errnum)))
        BAIL_OUT ("flux_response_derive failed");
    return rep;
}

flux_msg_t *create_request (uint32_t matchtag, int setflags, bool add_uuid)
{
    flux_msg_t *msg;
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_create failed");
    if (flux_msg_set_matchtag (msg, matchtag) < 0)
        BAIL_OUT ("flux_msg_set_matchtag failed");
    if (flux_msg_set_flag (msg, setflags) < 0)
        BAIL_OUT ("flux_msg_set_flag failed");

    flux_msg_route_enable (msg);
    if (add_uuid) {
        uuid_generate (uuid);
        uuid_unparse (uuid, uuid_str);
        if (flux_msg_route_push (msg, uuid_str) < 0)
            BAIL_OUT ("flux_msg_route_push failed");
        }
    return msg;
}

void purge (const flux_msg_t *msg, void *arg)
{
    int *count = arg;
    (*count)++;
}

void test_purge (void)
{
    struct rpc_track *rt;
    int count;
    flux_msg_t *msg[2];
    int i;

    msg[0] = create_request (1, 0, true);
    msg[1] = create_request (2, FLUX_MSGFLAG_STREAMING, true);

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");

    count = 0;
    rpc_track_purge (rt, purge, &count);
    ok (count == 0,
        "rpc_track_purge does nothing on empty hash");

    for (i = 0; i < ARRAY_SIZE (msg); i++)
        rpc_track_update (rt, msg[i]);
    ok (rpc_track_count (rt) == 2,
        "rpc_track_update tracks 2 messages");

    count = 0;
    rpc_track_purge (rt, purge, &count);
    ok (count == 2,
        "rpc_track_purge called callback 2 times");
    ok (rpc_track_count (rt) == 0,
        "rpc_track_purge emptied hash");

    rpc_track_destroy (rt);

    for (i = 0; i < ARRAY_SIZE (msg); i++)
        flux_msg_decref (msg[i]);
}

void test_basic (void)
{
    struct rpc_track *rt;
    flux_msg_t *req[4];
    flux_msg_t *rep[3];
    int i;

    req[0] = create_request (0, FLUX_MSGFLAG_NORESPONSE, true); // won't track
    req[1] = create_request (1, 0, true);
    req[2] = create_request (2, FLUX_MSGFLAG_STREAMING, true);
    req[3] = flux_msg_copy (req[2], true); // same as 2 except new matchtag
    if (!req[3])
        BAIL_OUT ("flux_msg_copy failed");
    if (flux_msg_set_matchtag (req[3], 3) < 0)
        BAIL_OUT ("flux_msg_set_matchtag failed");

    rep[0] = create_response (req[1], 0); // terminating (non-streaming)
    rep[1] = create_response (req[2], 1); // terminating
    rep[2] = create_response (req[3], 0); // not terminating (streaming)

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");

    ok (rpc_track_count (rt) == 0,
        "rpc_track_count returns 0 on empty hash");

    for (i = 0; i < ARRAY_SIZE (req); i++)
        rpc_track_update (rt, req[i]);
    ok (rpc_track_count (rt) == 3,
        "rpc_track_update works (3 of 4 requests tracked)");

    for (i = 0; i < ARRAY_SIZE (rep); i++)
        rpc_track_update (rt, rep[i]);
    ok (rpc_track_count (rt) == 1,
        "rpc_track_update works (2 requests terminated)");

    rpc_track_destroy (rt);

    for (i = 0; i < ARRAY_SIZE (req); i++)
        flux_msg_decref (req[i]);
    for (i = 0; i < ARRAY_SIZE (rep); i++)
        flux_msg_decref (rep[i]);
}

void test_disconnect (void)
{
    struct rpc_track *rt;
    flux_msg_t *req[4];
    flux_msg_t *dis;
    int i;

    req[0] = create_request (0, FLUX_MSGFLAG_NORESPONSE, true); // won't track
    req[1] = create_request (1, 0, true);
    req[2] = create_request (2, FLUX_MSGFLAG_STREAMING, true);
    req[3] = flux_msg_copy (req[2], true); // same as 2 except new matchtag
    if (!req[3])
        BAIL_OUT ("flux_msg_copy failed");
    if (flux_msg_set_matchtag (req[3], 3) < 0)
        BAIL_OUT ("flux_msg_set_matchtag failed");
    dis = create_disconnect (req[2]);

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");

    for (i = 0; i < ARRAY_SIZE (req); i++)
        rpc_track_update (rt, req[i]);
    ok (rpc_track_count (rt) == 3,
        "rpc_track_update works (3 of 4 requests tracked)");

    rpc_track_update (rt, dis);
    ok (rpc_track_count (rt) == 1, // 2 of 3 match the disconnect
        "rpc_track_update correctly processed disconnect request");

    rpc_track_destroy (rt);

    for (i = 0; i < ARRAY_SIZE (req); i++)
        flux_msg_decref (req[i]);
    flux_msg_decref (dis);
}

void test_badarg (void)
{
    struct rpc_track *rt;
    flux_msg_t *msg;
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);

    errno = 0;
    ok (rpc_track_create (42) == NULL && errno == EINVAL,
        "rpc_track_create type=42 fails with EINVAL");

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");

    rpc_track_update (rt, NULL);
    ok (rpc_track_count (rt) == 0,
        "rpc_track_update msg=NULL is a no-op");

    if (!(msg = flux_request_encode ("foo", NULL))
        || flux_msg_set_matchtag (msg, 1) < 0)
        BAIL_OUT ("could not create test message");
    rpc_track_update (rt, NULL);
    ok (rpc_track_count (rt) == 0,
        "rpc_track_update msg=(no sender) is a no-op");
    flux_msg_decref (msg);

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("could not create test message");
    flux_msg_route_enable (msg);
    if (flux_msg_route_push (msg, uuid_str) < 0)
        BAIL_OUT ("could not tweak test message");
    rpc_track_update (rt, NULL);
    ok (rpc_track_count (rt) == 0,
        "rpc_track_update msg=(no matchtag) is a no-op");
    flux_msg_decref (msg);

    if (!(msg = flux_event_encode ("meep", NULL)))
        BAIL_OUT ("could not create test message");
    rpc_track_update (rt, NULL);
    ok (rpc_track_count (rt) == 0,
        "rpc_track_update msg=event is a no-op");
    flux_msg_decref (msg);

    if (!(msg = flux_control_encode (42, 43)))
        BAIL_OUT ("could not create test message");
    rpc_track_update (rt, NULL);
    ok (rpc_track_count (rt) == 0,
        "rpc_track_update msg=control is a no-op");
    flux_msg_decref (msg);

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("could not create test message");
    flux_msg_route_enable (msg);
    if (flux_msg_route_push (msg, uuid_str) < 0
        || flux_msg_set_matchtag (msg, 1) < 0)
        BAIL_OUT ("could not tweak test message");
    rpc_track_update (rt, msg);
    if (rpc_track_count (rt) != 1)
        BAIL_OUT ("could not track legit request");
    flux_msg_decref (msg);  // reminder: hash has one entry

    if (!(msg = flux_request_encode ("foo.disconnect", NULL))
        || flux_msg_set_noresponse (msg) < 0)
        BAIL_OUT ("could not create test disconnect");
    rpc_track_update (rt, msg);
    ok (rpc_track_count (rt) == 1,
        "a disconnect without a uuid has no effect");
    flux_msg_decref (msg);

    rpc_track_destroy (rt);
}

/* Will it hash?
 */
void test_hashable (void)
{
    struct rpc_track *rt;
    flux_msg_t *msg;
    int count;

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");

    count = rpc_track_count (rt);
    msg = create_request (1, 0, false);
    rpc_track_update (rt, msg);
    ok (rpc_track_count (rt) - count == 1,
        "message 1 with valid matchtag, missing uuid is tracked");
    flux_msg_decref (msg);

    count = rpc_track_count (rt);
    msg = create_request (2, 0, false);
    rpc_track_update (rt, msg);
    ok (rpc_track_count (rt) - count == 1,
        "message 2 with new matchtag, missing uuid is tracked");
    flux_msg_decref (msg);

    /* This one is like RFC 27 sched alloc RPC, which sets matchtag to
     * FLUX_MATCHTAG_NONE, but does not set FLUX_MSGFLAG_NORESPONSE flag.
     */
    count = rpc_track_count (rt);
    msg = create_request (FLUX_MATCHTAG_NONE, 0, true);
    rpc_track_update (rt, msg);
    ok (rpc_track_count (rt) - count == 0,
        "message with no matchtag, valid uuid is not tracked");
    flux_msg_decref (msg);

    rpc_track_destroy (rt);
}

void test_nilarg (void)
{
    struct rpc_track *rt;
    flux_msg_t *msg;

    if (!(rt = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("rpc_track_create failed");
    msg = create_request (1, 0, true);
    rpc_track_update (rt, msg);

    ok (rpc_track_count (NULL) == 0,
        "rpc_track_count rt=NULL returns 0");

    lives_ok ({rpc_track_update (NULL, msg);},
              "rpc_track_update rt=NULL doesn't crash");
    lives_ok ({rpc_track_update (rt, NULL);},
              "rpc_track_update msg=NULL doesn't crash");

    lives_ok ({rpc_track_purge (NULL, purge, NULL);},
              "rpc_track_purge rt=NULL doesn't crash");

    lives_ok ({rpc_track_purge (rt, NULL, NULL);},
              "rpc_track_purge func=NULL doesn't crash");
    lives_ok ({rpc_track_destroy (NULL);},
              "rpc_track_destroy rt=NULL doesn't crash");


    flux_msg_decref (msg);
    rpc_track_destroy (rt);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_purge ();
    test_disconnect ();
    test_badarg ();
    test_hashable ();
    test_nilarg ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

