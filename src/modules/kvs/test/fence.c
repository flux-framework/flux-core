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

void fence_basic_tests (void)
{
    fence_t *f;
    json_t *ops;
    json_t *o;
    flux_msg_t *request;
    const char *name;
    int count = 0;

    ok (fence_create (NULL, 0, 0) == NULL,
        "fence_create fails on bad input");

    ok ((f = fence_create ("foo", 1, 3)) != NULL,
        "fence_create works");

    ok (fence_count_reached (f) == false,
        "initial fence_count_reached() is false");

    ok ((name = fence_get_name (f)) != NULL,
        "fence_get_name works");

    ok (strcmp (name, "foo") == 0,
        "fence_get_name returns the correct name");

    ok (fence_get_nprocs (f) == 1,
        "fence_get_nprocs works");

    ok (fence_get_flags (f) == 3,
        "fence_get_flags works");

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

    ok (fence_get_processed (f) == false,
        "fence_get_processed returns false initially");

    fence_set_processed (f, true);

    ok (fence_get_processed (f) == true,
        "fence_get_processed returns true");

    ok (fence_get_aux_int (f) == 0,
        "fence_get_aux_int returns 0 initially");

    fence_set_aux_int (f, 5);

    ok (fence_get_aux_int (f) == 5,
        "fence_get_aux_int returns 5 after set");

    flux_msg_destroy (request);

    fence_destroy (f);
}

void fence_ops_tests (void)
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

void fence_request_tests (void)
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

void fence_mgr_basic_tests (void)
{
    fence_mgr_t *fm;
    fence_t *f, *tf;

    ok ((fm = fence_mgr_create ()) != NULL,
        "fence_mgr_create works");

    ok (fence_mgr_fences_count (fm) == 0,
        "fence_mgr_fences_count returns 0 when no fences added");

    ok ((f = fence_create ("fence1", 1, 0)) != NULL,
        "fence_create works");

    ok (fence_mgr_add_fence (fm, f) == 0,
        "fence_mgr_add_fence works");

    ok (fence_mgr_add_fence (fm, f) < 0,
        "fence_mgr_add_fence fails on duplicate fence");

    ok ((tf = fence_mgr_lookup_fence (fm, "fence1")) != NULL,
        "fence_mgr_lookup_fence works");

    ok (f == tf,
        "fence_mgr_lookup_fence returns correct fence");

    ok (fence_mgr_lookup_fence (fm, "invalid") == NULL,
        "fence_mgr_lookup_fence can't find invalid fence");

    ok (fence_mgr_fences_count (fm) == 1,
        "fence_mgr_fences_count returns 1 when fence submitted");

    fence_mgr_remove_fence (fm, "fence1");

    ok (fence_mgr_fences_count (fm) == 0,
        "fence_mgr_fences_count returns 0 after fence remove");

    ok (fence_mgr_lookup_fence (fm, "fence1") == NULL,
        "fence_mgr_lookup_fence can't find removed fence");

    fence_mgr_destroy (fm);
}

int fence_count_cb (fence_t *f, void *data)
{
    int *count = data;
    (*count)++;
    return 0;
}

int fence_remove_cb (fence_t *f, void *data)
{
    fence_mgr_t *fm = data;

    fence_mgr_remove_fence (fm, fence_get_name (f));
    return 0;
}

int fence_add_error_cb (fence_t *f, void *data)
{
    fence_mgr_t *fm = data;
    fence_t *f2;

    f2 = fence_create ("foobar", 1, 0);

    if (fence_mgr_add_fence (fm, f2) < 0)
        return -1;
    return 0;
}

int fence_error_cb (fence_t *f, void *data)
{
    return -1;
}

void fence_mgr_iter_tests (void)
{
    fence_mgr_t *fm;
    fence_t *f;
    int count;

    ok ((fm = fence_mgr_create ()) != NULL,
        "fence_mgr_create works");

    count = 0;
    ok (fence_mgr_iter_fences (fm, fence_count_cb, &count) == 0
        && count == 0,
        "fence_mgr_iter_fences success when no fences submitted");

    ok ((f = fence_create ("fence1", 1, 0)) != NULL,
        "fence_create works");

    ok (fence_mgr_add_fence (fm, f) == 0,
        "fence_mgr_add_fence works");

    ok (fence_mgr_fences_count (fm) == 1,
        "fence_mgr_fences_count returns correct count of fences");

    ok (fence_mgr_iter_fences (fm, fence_error_cb, NULL) < 0,
        "fence_mgr_iter_fences error on callback error");

    ok (fence_mgr_iter_fences (fm, fence_add_error_cb, fm) < 0
        && errno == EAGAIN,
        "fence_mgr_iter_fences error on callback error trying to add fence");

    ok (fence_mgr_iter_fences (fm, fence_remove_cb, fm) == 0,
        "fence_mgr_iter_fences success on remove");

    count = 0;
    ok (fence_mgr_iter_fences (fm, fence_count_cb, &count) == 0,
        "fence_mgr_iter_fences success on count");

    ok (count == 0,
        "fence_mgr_iter_fences returned correct count of fences");

    ok (fence_mgr_fences_count (fm) == 0,
        "fence_mgr_fences_count returns correct count of fences");

    fence_mgr_destroy (fm);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    fence_basic_tests ();
    fence_ops_tests ();
    fence_request_tests ();
    fence_mgr_basic_tests ();
    fence_mgr_iter_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
