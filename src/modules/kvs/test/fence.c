#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libkvs/kvs.h"
#include "src/common/libflux/message.h"
#include "src/common/libflux/request.h"
#include "src/modules/kvs/fence.h"

int msg_cb (fence_t *f, const flux_msg_t *req, void *data)
{
    int *count = data;
    const char *topic;

    if (!flux_msg_get_topic (req, &topic)
        && !strcmp (topic, "mytopic"))
        (*count)++;

    return 0;
}

int msg_cb_error (fence_t *f, const flux_msg_t *req, void *data)
{
    return -1;
}

void basic_api_tests (void)
{
    fence_t *f;
    json_t *names, *ops;
    json_t *o;
    flux_msg_t *request;
    int count = 0;

    ok ((f = fence_create ("foo", 1, 3)) != NULL,
        "fence_create works");

    ok (fence_count_reached (f) == false,
        "initial fence_count_reached() is false");

    ok (fence_get_nprocs (f) == 1,
        "fence_get_nprocs works");

    ok (fence_get_flags (f) == 3,
        "fence_get_flags works");

    ok ((o = fence_get_json_names (f)) != NULL,
        "initial fence_get_json_names works");

    names = json_array ();
    json_array_append_new (names, json_string ("foo"));

    ok (json_equal (names, o) == true,
        "initial fence_get_json_names match");

    json_decref (names);

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));

    ok (fence_add_request_ops (f, ops) == 0,
        "initial fence_add_request_ops add works");

    ok ((o = fence_get_json_ops (f)) != NULL,
        "initial fence_get_json_ops call works");

    ok (json_equal (ops, o) == true,
        "initial fence_get_json_ops match");

    ok (fence_add_request_ops (f, ops) < 0
        && errno == EOVERFLOW,
        "fence_add_request_ops fails with EOVERFLOW when exceeding nprocs");

    json_decref (ops);

    ok (fence_iter_request_copies (f, msg_cb, &count) == 0,
        "initial fence_iter_request_copies works");

    ok (count == 0,
        "initial fence_iter_request_copies count is 0");

    ok ((request = flux_request_encode ("mytopic", "{ bar : 1 }")) != NULL,
        "flux_request_encode works");

    ok (fence_add_request_copy (f, request) == 0,
        "initial fence_add_request_copy call works");

    ok (fence_iter_request_copies (f, msg_cb, &count) == 0,
        "second fence_iter_request_copies works");

    ok (count == 1,
        "second fence_iter_request_copies count is 1");

    ok (fence_count_reached (f) == true,
        "later fence_count_reached() is true");

    ok (fence_get_aux_int (f) == 0,
        "fence_get_aux_int returns 0 initially");

    fence_set_aux_int (f, 5);

    ok (fence_get_aux_int (f) == 5,
        "fence_get_aux_int returns 5 after set");

    flux_msg_destroy (request);

    fence_destroy (f);
}

void ops_tests (void)
{
    fence_t *f;
    json_t *ops;
    json_t *o;

    ok ((f = fence_create ("foo", 3, 3)) != NULL,
        "fence_create works");

    ok (fence_count_reached (f) == false,
        "initial fence_count_reached() is false");

    ok (fence_add_request_ops (f, NULL) == 0,
        "fence_add_request_ops works with NULL ops");

    ok (fence_count_reached (f) == false,
        "fence_count_reached() is still false");

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));

    ok (fence_add_request_ops (f, ops) == 0,
        "fence_add_request_ops add works");

    json_decref (ops);

    ok (fence_count_reached (f) == false,
        "fence_count_reached() is still false");

    /* for test ops can be anything */
    ops = json_array ();
    json_array_append_new (ops, json_string ("B"));

    ok (fence_add_request_ops (f, ops) == 0,
        "fence_add_request_ops add works");

    json_decref (ops);

    ok (fence_count_reached (f) == true,
        "fence_count_reached() is true");

    ok ((o = fence_get_json_ops (f)) != NULL,
        "initial fence_get_json_ops call works");

    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));
    json_array_append_new (ops, json_string ("B"));

    ok (json_equal (ops, o) == true,
        "fence_get_json_ops match");

    json_decref (ops);

    fence_destroy (f);
}

void request_tests (void)
{
    fence_t *f;
    flux_msg_t *request;
    int count = 0;

    ok ((f = fence_create ("foo", 1, 3)) != NULL,
        "fence_create works");

    ok (fence_iter_request_copies (f, msg_cb, &count) == 0,
        "initial fence_iter_request_copies works");

    ok (count == 0,
        "initial fence_iter_request_copies count is 0");

    ok ((request = flux_request_encode ("mytopic", "{ A : 1 }")) != NULL,
        "flux_request_encode works");

    ok (fence_add_request_copy (f, request) == 0,
        "fence_add_request_copy works");

    flux_msg_destroy (request);

    ok ((request = flux_request_encode ("mytopic", "{ B : 1 }")) != NULL,
        "flux_request_encode works");

    ok (fence_add_request_copy (f, request) == 0,
        "fence_add_request_copy works");

    flux_msg_destroy (request);

    ok (fence_iter_request_copies (f, msg_cb_error, &count) == -1,
        "fence_iter_request_copies errors when cb errors");

    ok (fence_iter_request_copies (f, msg_cb, &count) == 0,
        "second fence_iter_request_copies works");

    ok (count == 2,
        "fence_iter_request_copies count is 2");

    fence_destroy (f);
}

fence_t *create_fence (const char *name, const char *opname, int flags)
{
    fence_t *f;
    json_t *ops;

    ok ((f = fence_create (name, 1, flags)) != NULL,
        "fence_create works");

    ops = json_array ();
    json_array_append_new (ops, json_string (opname));

    ok (fence_add_request_ops (f, ops) == 0,
        "fence_add_request_ops add works");

    json_decref (ops);

    return f;
}

void merge_tests (void)
{
    fence_t *f1, *f2;
    json_t *names, *ops;
    json_t *o;

    f1 = create_fence ("foo", "A", 0);
    f2 = create_fence ("bar", "B", 0);

    ok (fence_merge (f1, f2) == 1,
        "fence_merge success");

    ok ((o = fence_get_json_names (f1)) != NULL,
        "fence_get_json_names works");

    names = json_array ();
    json_array_append_new (names, json_string ("foo"));
    json_array_append_new (names, json_string ("bar"));

    ok (json_equal (names, o) == true,
        "fence_get_json_names match");

    json_decref (names);

    ok ((o = fence_get_json_ops (f1)) != NULL,
        "fence_get_json_ops works");

    ops = json_array ();
    json_array_append_new (ops, json_string ("A"));
    json_array_append_new (ops, json_string ("B"));

    ok (json_equal (ops, o) == true,
        "fence_get_json_ops match");

    fence_destroy (f1);
    fence_destroy (f2);

    f1 = create_fence ("foo", "A", FLUX_KVS_NO_MERGE);
    f2 = create_fence ("bar", "B", 0);

    ok (fence_merge (f1, f2) == 0,
        "fence_merge no merge");

    fence_destroy (f1);
    fence_destroy (f2);

    f1 = create_fence ("foo", "A", 0);
    f2 = create_fence ("bar", "B", FLUX_KVS_NO_MERGE);

    ok (fence_merge (f1, f2) == 0,
        "fence_merge no merge");

    fence_destroy (f1);
    fence_destroy (f2);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic_api_tests ();
    ops_tests ();
    request_tests ();
    merge_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
