/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libidset/idset.h"

#include "src/modules/resource/rutil.h"

const char *by_rank = \
"{"\
"\"0-3\": {\"Package\": 1, \"Core\": 2, \"PU\": 2, \"cpuset\": \"0-1\"}," \
"\"4-31\": {\"Package\": 2, \"Core\": 4, \"PU\": 4, \"cpuset\": \"0-3\"}" \
"}";

void test_match_request_sender (void)
{
    flux_msg_t *msg1, *msg2;

    if (!(msg1 = flux_request_encode ("fubar.baz", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (!(msg2 = flux_request_encode ("fubaz.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok (rutil_match_request_sender (msg1, NULL) == false,
        "rutil_match_request_sender msg2=NULL = false");
    ok (rutil_match_request_sender (NULL, msg2) == false,
        "rutil_match_request_sender msg1=NULL = false");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender msg1=(no sender) = false");

    if (flux_msg_push_route (msg1, "foo") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender msg2=(no sender) = false");

    if (flux_msg_push_route (msg2, "bar") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == false,
        "rutil_match_request_sender different senders = false");

    char *id;
    if (flux_msg_pop_route (msg2, &id) < 0)
        BAIL_OUT ("flux_msg_clear_route failed");
    free (id);
    if (flux_msg_push_route (msg2, "foo") < 0)
        BAIL_OUT ("flux_msg_push_route failed");

    ok (rutil_match_request_sender (msg1, msg2) == true,
        "rutil_match_request_sender same senders = true");

    flux_msg_decref (msg1);
    flux_msg_decref (msg2);
}

void test_idset_sub (void)
{
    struct idset *ids1;
    struct idset *ids2;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (rutil_idset_sub (NULL, ids2) < 0 && errno == EINVAL,
        "rutil_idset_sub ids1=NULL fails with EINVAL");

    ok (rutil_idset_sub (ids1, NULL) == 0 && idset_count (ids1) == 0,
        "rutil_idset_sub ids2=NULL has no effect");

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 42) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_sub (ids1, ids2) == 0 && idset_count (ids1) == 1,
        "rutil_idset_sub non-overlapping idsets has no effect");

    if (idset_set (ids1, 42) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 2) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_sub (ids1, ids2) == 0 && idset_count (ids1) == 0,
        "rutil_idset_sub with overlap works");

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_idset_add (void)
{
    struct idset *ids1;
    struct idset *ids2;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    errno = 0;
    ok (rutil_idset_add (NULL, ids2) < 0 && errno == EINVAL,
        "rutil_idset_add ids1=NULL fails with EINVAL");

    ok (rutil_idset_add (ids1, NULL) == 0 && idset_count (ids1) == 0,
        "rutil_idset_add ids2=NULL has no effect");

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    if (idset_set (ids2, 42) < 0)
        BAIL_OUT ("idset_set failed");

    ok (rutil_idset_add (ids1, ids2) == 0 && idset_count (ids1) == 2,
        "rutil_idset_add of non-overlapping idset works");
    ok (rutil_idset_add (ids1, ids2) == 0 && idset_count (ids1) == 2,
        "rutil_idset_add of overlapping idset has no effect");

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_idset_diff (void)
{
    struct idset *ids1;
    struct idset *ids2;
    struct idset *add;
    struct idset *sub;

    if (!(ids1 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (!(ids2 = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");

    ok (rutil_idset_diff (NULL, ids2, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff ids1=NULL works");
    idset_destroy (add);
    idset_destroy (sub);

    ok (rutil_idset_diff (ids1, NULL, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff ids2=NULL works");
    idset_destroy (add);
    idset_destroy (sub);

    errno = 0;
    ok (rutil_idset_diff (ids1, ids2, NULL, &sub) < 0 && errno == EINVAL,
        "rutil_idset_diff add=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_idset_diff (ids1, ids2, &add, NULL) < 0 && errno == EINVAL,
        "rutil_idset_diff sub=NULL fails with EINVAL");

    if (idset_set (ids1, 1) < 0 || idset_set (ids2, 2) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 2)
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 1),
        "rutil_idset_diff [1] [2] sets add=[2] sub=[1]");
    idset_destroy (add);
    idset_destroy (sub);

    add = sub = NULL;
    ok (rutil_idset_diff (ids2, ids1, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 1)
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 2),
        "rutil_idset_diff [2] [1] sets add=[1] sub=[2]");
    idset_destroy (add);
    idset_destroy (sub);

    if (idset_set (ids1, 2) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add == NULL
        && sub != NULL && idset_count (sub) == 1 && idset_test (sub, 1),
        "rutil_idset_diff [1-2] [2] sets add=NULL sub=[1]");
    idset_destroy (add);
    idset_destroy (sub);

    add = sub = NULL;
    ok (rutil_idset_diff (ids2, ids1, &add, &sub) == 0
        && add != NULL && idset_count (add) == 1 && idset_test (add, 1)
        && sub == NULL,
        "rutil_idset_diff [2] [1-2] sets add=[1] sub=NULL");
    idset_destroy (add);
    idset_destroy (sub);

    if (idset_set (ids2, 1) < 0)
        BAIL_OUT ("idset_set failed");
    add = sub = NULL;
    ok (rutil_idset_diff (ids1, ids2, &add, &sub) == 0
        && add == NULL
        && sub == NULL,
        "rutil_idset_diff [1-2] [1-2] sets add=NULL sub=NULL");
    idset_destroy (add);
    idset_destroy (sub);

    idset_destroy (ids1);
    idset_destroy (ids2);
}

void test_set_json_idset (void)
{
    json_t *o;
    json_t *o2;
    const char *s;
    struct idset *ids;

    if (!(ids= idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (idset_set (ids, 42) < 0)
        BAIL_OUT ("idset_set failed");

    if (!(o = json_object ()))
        BAIL_OUT ("json_object failed");

    errno = 0;
    ok (rutil_set_json_idset (NULL, "foo", NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset obj=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_set_json_idset (o, NULL, NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset key=NULL fails with EINVAL");
    errno = 0;
    ok (rutil_set_json_idset (o, "", NULL) < 0 && errno == EINVAL,
        "rutil_set_json_idset key=(empty) fails with EINVAL");

    ok (rutil_set_json_idset (o, "foo", NULL) == 0
            && (o2 = json_object_get (o, "foo"))
            && (s = json_string_value (o2))
            && !strcmp (s, ""),
        "rutil_set_json_idset ids=NULL sets empty string value");
    ok (rutil_set_json_idset (o, "bar", ids) == 0
            && (o2 = json_object_get (o, "bar"))
            && (s = json_string_value (o2))
            && !strcmp (s, "42"),
        "rutil_set_json_idset ids=[42] sets encoded value");

    json_decref (o);
    idset_destroy (ids);
}

void test_idset_from_resobj (void)
{
    json_t *resobj;
    struct idset *ids;

    if (!(resobj = json_loads (by_rank, 0, NULL)))
        BAIL_OUT ("json_loads failed");

    ids = rutil_idset_from_resobj (NULL);
    ok (ids != NULL && idset_count (ids) == 0,
        "rutil_idset_from_resobj NULL returns empty idset");
    idset_destroy (ids);

    ids = rutil_idset_from_resobj (resobj);
    ok (ids != NULL && idset_count (ids) == 32,
        "rutil_idset_from_resobj works");
    idset_destroy (ids);

    json_decref (resobj);
}

void test_resobj_sub (void)
{
    json_t *resobj1;
    json_t *resobj2;
    struct idset *ids1;
    struct idset *ids2;
    struct idset *ids;

    if (!(resobj1 = json_loads (by_rank, 0, NULL)))
        BAIL_OUT ("json_loads failed");
    if (!(ids1 = rutil_idset_from_resobj (resobj1)))
        BAIL_OUT ("rutil_idset_from_resobj failed");

    if (!(ids = idset_create (1024, 0)))
        BAIL_OUT ("idset_create failed");
    if (idset_range_set (ids, 2, 5) < 0)
        BAIL_OUT ("idset_range_set failed");

    errno = 0;
    ok (rutil_resobj_sub (NULL, NULL) == NULL && errno == EINVAL,
        "rutil_resobj_sub resobj=NULL fails with EINVAL");

    resobj2 = rutil_resobj_sub (resobj1, NULL);
    ok (resobj2 != NULL && json_equal (resobj1, resobj2) == 1,
        "rutil_resobj_sub ids=NULL returns unchanged resobj");
    if (!(ids2 = rutil_idset_from_resobj (resobj2)))
        BAIL_OUT ("rutil_idset_from_resobj failed");
    ok (idset_equal (ids1, ids2) == true,
        "new and old resobj have identical idset");
    idset_destroy (ids2);
    json_decref (resobj2);

    resobj2 = rutil_resobj_sub (resobj1, ids);
    ok (resobj2 != NULL && json_equal (resobj1, resobj2) == 0,
        "rutil_resobj_sub ids=[2-5] returns different resobj");
    if (!(ids2 = rutil_idset_from_resobj (resobj2)))
        BAIL_OUT ("rutil_idset_from_resobj failed");
    ok (idset_count (ids1) - idset_count (ids2) == 4,
        "new and old resobj idset differ by 4 ids");
    idset_destroy (ids2);
    json_decref (resobj2);

    /* Kill one whole key in the resobj */
    if (idset_range_set (ids, 0, 3) < 0)
        BAIL_OUT ("idset_range_set failed");
    resobj2 = rutil_resobj_sub (resobj1, ids);
    ok (resobj2 != NULL && json_object_size (resobj2) == 1,
        "rutil_resobj_sub ids=[0-5] returns resobj with 1 key");
    json_decref (resobj2);

    /* Kill entire resobj */
    if (idset_range_set (ids, 0, 31) < 0)
        BAIL_OUT ("idset_range_set failed");
    resobj2 = rutil_resobj_sub (resobj1, ids);
    ok (resobj2 != NULL && json_object_size (resobj2) == 0,
        "rutil_resobj_sub ids=[0-31] returns resobj with no keys");
    json_decref (resobj2);

    idset_destroy (ids);
    idset_destroy (ids1);
    json_decref (resobj1);
}

void test_idset_decode_test (void)
{
    ok (rutil_idset_decode_test (NULL, 0) == false,
        "rutil_idset_decode_test idset=NULL returns false");
    ok (rutil_idset_decode_test ("", 0) == false,
        "rutil_idset_decode_test idset=\"\" id=0 returns false");
    ok (rutil_idset_decode_test ("0", 0) == true,
        "rutil_idset_decode_test idset=\"0\" id=0 returns true");
    ok (rutil_idset_decode_test ("0", 1) == false,
        "rutil_idset_decode_test idset=\"0\" id=1 returns false");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_match_request_sender ();
    test_idset_sub ();
    test_idset_add ();
    test_idset_diff ();
    test_set_json_idset ();
    test_idset_from_resobj ();
    test_resobj_sub ();
    test_idset_decode_test ();

    done_testing ();
    return (0);
}


/*
 * vi:ts=4 sw=4 expandtab
 */
