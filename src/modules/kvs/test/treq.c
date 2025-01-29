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

int msg_cb (treq_t *tr, const flux_msg_t *req, void *data)
{
    int *count = data;
    const char *topic;

    if (!flux_msg_get_topic (req, &topic)
        && streq (topic, "mytopic"))
        (*count)++;

    return 0;
}

int msg_cb_error (treq_t *tr, const flux_msg_t *req, void *data)
{
    return -1;
}

void treq_basic_tests (void)
{
    treq_t *tr;
    json_t *ops;
    json_t *o;
    flux_msg_t *request;
    const char *name;
    int count = 0;

    ok (treq_create (NULL, 0, 0) == NULL,
        "treq_create fails on bad input");

    ok ((tr = treq_create ("foo", 1, 3)) != NULL,
        "treq_create works");

    ok (treq_count_reached (tr) == false,
        "initial treq_count_reached() is false");

    ok ((name = treq_get_name (tr)) != NULL,
        "treq_get_name works");

    ok (streq (name, "foo"),
        "treq_get_name returns the correct name");

    ok (treq_get_nprocs (tr) == 1,
        "treq_get_nprocs works");

    ok (treq_get_flags (tr) == 3,
        "treq_get_flags works");

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));

    ok (treq_add_request_ops (tr, ops) == 0,
        "initial treq_add_request_ops add works");

    ok ((o = treq_get_ops (tr)) != NULL,
        "initial treq_get_ops call works");

    ok (json_equal (ops, o) == true,
        "initial treq_get_ops match");

    ok (treq_add_request_ops (tr, ops) < 0
        && errno == EOVERFLOW,
        "treq_add_request_ops fails with EOVERFLOW when exceeding nprocs");

    json_decref (ops);

    ok (treq_iter_request_copies (tr, msg_cb, &count) == 0,
        "initial treq_iter_request_copies works");

    ok (count == 0,
        "initial treq_iter_request_copies count is 0");

    ok ((request = flux_request_encode ("mytopic", "{ bar : 1 }")) != NULL,
        "flux_request_encode works");

    ok (treq_add_request_copy (tr, request) == 0,
        "initial treq_add_request_copy call works");

    ok (treq_iter_request_copies (tr, msg_cb, &count) == 0,
        "second treq_iter_request_copies works");

    ok (count == 1,
        "second treq_iter_request_copies count is 1");

    ok (treq_count_reached (tr) == true,
        "later treq_count_reached() is true");

    ok (treq_get_processed (tr) == false,
        "treq_get_processed returns false initially");

    treq_mark_processed (tr);

    ok (treq_get_processed (tr) == true,
        "treq_get_processed returns true");

    flux_msg_destroy (request);

    treq_destroy (tr);

    ok (treq_create_rank (1, 2, -1, 0) == NULL,
        "treq_create_rank fails on bad input");

    ok ((tr = treq_create_rank (214, 3577, 2, 4)) != NULL,
        "treq_create_rank works");

    ok ((name = treq_get_name (tr)) != NULL,
        "treq_get_name works");

    ok (strstr (name, "214") != NULL,
        "treq_get_name returns name with rank in it");

    ok (strstr (name, "3577") != NULL,
        "treq_get_name returns name with seq in it");

    treq_destroy (tr);
}

void treq_ops_tests (void)
{
    treq_t *tr;
    json_t *ops;
    json_t *o;

    ok ((tr = treq_create ("foo", 3, 3)) != NULL,
        "treq_create works");

    ok (treq_count_reached (tr) == false,
        "initial treq_count_reached() is false");

    ok (treq_add_request_ops (tr, NULL) == 0,
        "treq_add_request_ops works with NULL ops");

    ok (treq_count_reached (tr) == false,
        "treq_count_reached() is still false");

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));

    ok (treq_add_request_ops (tr, ops) == 0,
        "treq_add_request_ops add works");

    json_decref (ops);

    ok (treq_count_reached (tr) == false,
        "treq_count_reached() is still false");

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("B"));

    ok (treq_add_request_ops (tr, ops) == 0,
        "treq_add_request_ops add works");

    json_decref (ops);

    ok (treq_count_reached (tr) == true,
        "treq_count_reached() is true");

    ok ((o = treq_get_ops (tr)) != NULL,
        "initial treq_get_ops call works");

    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));
    json_array_append_new (ops, json_string ("B"));

    ok (json_equal (ops, o) == true,
        "treq_get_ops match");

    json_decref (ops);

    treq_destroy (tr);
}

void treq_request_tests (void)
{
    treq_t *tr;
    flux_msg_t *request;
    int count = 0;

    ok ((tr = treq_create ("foo", 1, 3)) != NULL,
        "treq_create works");

    ok (treq_iter_request_copies (tr, msg_cb, &count) == 0,
        "initial treq_iter_request_copies works");

    ok (count == 0,
        "initial treq_iter_request_copies count is 0");

    ok ((request = flux_request_encode ("mytopic", "{ A : 1 }")) != NULL,
        "flux_request_encode works");

    ok (treq_add_request_copy (tr, request) == 0,
        "treq_add_request_copy works");

    flux_msg_destroy (request);

    ok ((request = flux_request_encode ("mytopic", "{ B : 1 }")) != NULL,
        "flux_request_encode works");

    ok (treq_add_request_copy (tr, request) == 0,
        "treq_add_request_copy works");

    flux_msg_destroy (request);

    ok (treq_iter_request_copies (tr, msg_cb_error, &count) == -1,
        "treq_iter_request_copies errors when cb errors");

    ok (treq_iter_request_copies (tr, msg_cb, &count) == 0,
        "second treq_iter_request_copies works");

    ok (count == 2,
        "treq_iter_request_copies count is 2");

    treq_destroy (tr);
}

void treq_mgr_basic_tests (void)
{
    treq_mgr_t *trm;
    treq_t *tr, *tmp_tr;

    ok ((trm = treq_mgr_create ()) != NULL,
        "treq_mgr_create works");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 when no transactions added");

    ok ((tr = treq_create ("treq1", 1, 0)) != NULL,
        "treq_create works");

    ok (treq_mgr_add_transaction (trm, tr) == 0,
        "treq_mgr_add_transaction works");

    ok (treq_mgr_add_transaction (trm, tr) < 0,
        "treq_mgr_add_transaction fails on duplicate treq");

    ok ((tmp_tr = treq_mgr_lookup_transaction (trm, "treq1")) != NULL,
        "treq_mgr_lookup_transaction works");

    ok (tr == tmp_tr,
        "treq_mgr_lookup_transaction returns correct treq");

    ok (treq_mgr_lookup_transaction (trm, "invalid") == NULL,
        "treq_mgr_lookup_transaction can't find invalid treq");

    ok (treq_mgr_transactions_count (trm) == 1,
        "treq_mgr_transactions_count returns 1 when treq submitted");

    treq_mgr_remove_transaction (trm, "treq1");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns 0 after treq remove");

    ok (treq_mgr_lookup_transaction (trm, "treq1") == NULL,
        "treq_mgr_lookup_transaction can't find removed treq");

    treq_mgr_destroy (trm);
}

int treq_count_cb (treq_t *tr, void *data)
{
    int *count = data;
    (*count)++;
    return 0;
}

int treq_remove_cb (treq_t *tr, void *data)
{
    treq_mgr_t *trm = data;

    treq_mgr_remove_transaction (trm, treq_get_name (tr));
    return 0;
}

int treq_add_error_cb (treq_t *tr, void *data)
{
    treq_mgr_t *trm = data;
    treq_t *tr2;

    tr2 = treq_create ("foobar", 1, 0);

    if (treq_mgr_add_transaction (trm, tr2) < 0) {
        treq_destroy (tr2);
        return -1;
    }
    return 0;
}

int treq_error_cb (treq_t *tr, void *data)
{
    return -1;
}

void treq_mgr_iter_tests (void)
{
    treq_mgr_t *trm;
    treq_t *tr;
    int count;

    ok ((trm = treq_mgr_create ()) != NULL,
        "treq_mgr_create works");

    count = 0;
    ok (treq_mgr_iter_transactions (trm, treq_count_cb, &count) == 0
        && count == 0,
        "treq_mgr_iter_transactions success when no transactions submitted");

    ok ((tr = treq_create ("treq1", 1, 0)) != NULL,
        "treq_create works");

    ok (treq_mgr_add_transaction (trm, tr) == 0,
        "treq_mgr_add_transaction works");

    ok (treq_mgr_transactions_count (trm) == 1,
        "treq_mgr_transactions_count returns correct count of transactions");

    ok (treq_mgr_iter_transactions (trm, treq_error_cb, NULL) < 0,
        "treq_mgr_iter_transactions error on callback error");

    ok (treq_mgr_iter_transactions (trm, treq_add_error_cb, trm) < 0
        && errno == EAGAIN,
        "treq_mgr_iter_transactions error on callback error trying to add treq");

    ok (treq_mgr_iter_transactions (trm, treq_remove_cb, trm) == 0,
        "treq_mgr_iter_transactions success on remove");

    count = 0;
    ok (treq_mgr_iter_transactions (trm, treq_count_cb, &count) == 0,
        "treq_mgr_iter_transactions success on count");

    ok (count == 0,
        "treq_mgr_iter_transactions returned correct count of transactions");

    ok (treq_mgr_transactions_count (trm) == 0,
        "treq_mgr_transactions_count returns correct count of transactions");

    treq_mgr_destroy (trm);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    treq_basic_tests ();
    treq_ops_tests ();
    treq_request_tests ();
    treq_mgr_basic_tests ();
    treq_mgr_iter_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
