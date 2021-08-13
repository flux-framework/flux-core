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

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librouter/msg_hash.h"
#include "src/common/libtap/tap.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

flux_msg_t *create_request (void)
{
    flux_msg_t *msg;
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_create failed");
    flux_msg_route_enable (msg);
    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);
    if (flux_msg_route_push (msg, uuid_str) < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    return msg;
}

void test_basic (void)
{
    zhashx_t *zh;
    flux_msg_t *req1;
    flux_msg_t *req2;
    flux_msg_t *rep1;
    flux_msg_t *rep2;

    errno = 0;
    ok (msg_hash_create (42) == NULL && errno == EINVAL,
        "msg_hash_create type=42 fails with EINVAL");

    if (!(zh = msg_hash_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        BAIL_OUT ("msg_hash_create failed");

    req1 = create_request ();
    req2 = create_request ();
    rep1 = flux_response_derive (req1, 0);
    rep2 = flux_response_derive (req2, 0);
    if (!rep1 || !rep2)
        BAIL_OUT ("flux_response_derive failed");

    ok (zhashx_insert (zh, req1, req1) == 0,
        "inserted first request");
    ok (zhashx_insert (zh, req2, req2) == 0,
        "inserted second request");
    ok (zhashx_size (zh) == 2,
        "hash size=2");

    zhashx_delete (zh, req1);
    ok (zhashx_size (zh) == 1,
        "delete first request (from response), now hash size=1");

    ok (zhashx_lookup (zh, rep1) == NULL,
        "lookup of response 1 fails");
    ok (zhashx_lookup (zh, rep2) != NULL,
        "lookup of response 2 works");

    flux_msg_decref (req1);
    flux_msg_decref (req2);
    flux_msg_decref (rep1);
    flux_msg_decref (rep2);

    zhashx_destroy (&zh);

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

