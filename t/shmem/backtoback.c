/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_t *h_cli, *h_srv;
    flux_msg_t *msg;
    int type;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE),
                  0);
    ok ((h_srv = flux_open ("shmem://test&bind", 0)) != NULL,
        "created server handle");
    ok ((h_cli = flux_open ("shmem://test&connect", 0)) != NULL,
        "created client handle");
    if (!h_cli || !h_srv)
        BAIL_OUT ("can't continue without client or server handle");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test request");
    ok (flux_send (h_cli, msg, 0) == 0, "sent request to server");
    flux_msg_destroy (msg);

    ok ((msg = flux_recv (h_srv, FLUX_MATCH_ANY, 0)) != NULL,
        "server received request");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "message is correct type");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "created test response");
    ok (flux_send (h_srv, msg, 0) == 0, "sent response to client");
    flux_msg_destroy (msg);

    ok ((msg = flux_recv (h_cli, FLUX_MATCH_ANY, 0)) != NULL,
        "client received response");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "message is correct type");
    flux_msg_destroy (msg);

    flux_close (h_cli);
    flux_close (h_srv);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
