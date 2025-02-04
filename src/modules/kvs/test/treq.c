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
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/common/libflux/message.h"
#include "src/common/libflux/request.h"
#include "src/modules/kvs/treq.h"
#include "ccan/str/str.h"

void treq_mgr_basic_tests (void)
{
    treq_mgr_t *trm;
    flux_msg_t *request;
    const flux_msg_t *msg;

    if (!(request = flux_request_encode ("mytopic", "{ bar : 1 }")))
        BAIL_OUT ("flux_request_encode");

    ok ((trm = treq_mgr_create ()) != NULL,
        "treq_mgr_create works");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 when no transactions added");

    ok (treq_mgr_add_transaction (trm, request, "myname") == 0,
        "treq_mgr_add_transaction works");

    ok (treq_mgr_add_transaction (trm, request, "myname") < 0
        && errno == EEXIST,
        "treq_mgr_add_transaction fails on duplicate request");

    ok ((msg = treq_mgr_lookup_transaction (trm, "myname")) != NULL,
        "treq_mgr_lookup_transaction works");

    ok (msg == request,
        "treq_mgr_lookup_transaction returns correct msg");

    ok (treq_mgr_lookup_transaction (trm, "invalid") == NULL,
        "treq_mgr_lookup_transaction can't find invalid msg");

    ok (treq_mgr_transactions_count (trm) == 1,
        "treq_mgr_transactions_count returns 1 when request saved");

    treq_mgr_remove_transaction (trm, "myname");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 after msg remove");

    ok (treq_mgr_lookup_transaction (trm, "myname") == NULL,
        "treq_mgr_lookup_transaction can't find removed msg");

    treq_mgr_destroy (trm);
    flux_msg_decref (request);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    treq_mgr_basic_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
