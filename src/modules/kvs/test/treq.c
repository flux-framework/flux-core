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

void treq_basic_tests (void)
{
    treq_t *tr;
    flux_msg_t *request;
    const flux_msg_t *tmp;
    const char *name;
    const char *topic;

    ok ((request = flux_request_encode ("mytopic", "{ bar : 1 }")) != NULL,
        "flux_request_encode works");

    ok ((tr = treq_create (request, 214, 3577)) != NULL,
        "treq_create works");

    ok ((name = treq_get_name (tr)) != NULL,
        "treq_get_name works");

    ok (strstr (name, "214") != NULL,
        "treq_get_name returns name with rank in it");

    ok (strstr (name, "3577") != NULL,
        "treq_get_name returns name with seq in it");

    ok ((tmp = treq_get_request (tr)) != NULL,
        "treq_get_request works");

    if (flux_msg_get_topic (tmp, &topic) < 0)
        BAIL_OUT ("flux_msg_get_topic");

    ok (streq (topic, "mytopic"),
        "treq_get_request returned correct request");

    flux_msg_destroy (request);

    treq_destroy (tr);
}

void treq_mgr_basic_tests (void)
{
    treq_mgr_t *trm;
    treq_t *tr, *tmp_tr;
    const char *tmp_name;
    char *name;

    ok ((trm = treq_mgr_create ()) != NULL,
        "treq_mgr_create works");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 when no transactions added");

    ok ((tr = treq_create (NULL, 214, 3577)) != NULL,
        "treq_create works");

    ok ((tmp_name = treq_get_name (tr)) != NULL,
        "treq_get_name works");

    if (!(name = strdup (tmp_name)))
        BAIL_OUT ("strdup");

    ok (treq_mgr_add_transaction (trm, tr) == 0,
        "treq_mgr_add_transaction works");

    ok (treq_mgr_add_transaction (trm, tr) < 0,
        "treq_mgr_add_transaction fails on duplicate treq");

    ok ((tmp_tr = treq_mgr_lookup_transaction (trm, name)) != NULL,
        "treq_mgr_lookup_transaction works");

    ok (tr == tmp_tr,
        "treq_mgr_lookup_transaction returns correct treq");

    ok (treq_mgr_lookup_transaction (trm, "invalid") == NULL,
        "treq_mgr_lookup_transaction can't find invalid treq");

    ok (treq_mgr_transactions_count (trm) == 1,
        "treq_mgr_transactions_count returns 1 when treq submitted");

    treq_mgr_remove_transaction (trm, name);

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 after treq remove");

    ok (treq_mgr_lookup_transaction (trm, name) == NULL,
        "treq_mgr_lookup_transaction can't find removed treq");

    treq_mgr_destroy (trm);
    free (name);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    treq_basic_tests ();
    treq_mgr_basic_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
