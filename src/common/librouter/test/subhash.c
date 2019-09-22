/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/librouter/subhash.h"

void test_topic_match (void)
{
    struct subhash *sub;

    sub = subhash_create ();
    ok (sub != NULL,
        "subhash_create works");

    /* give foo a refcount of 2 */
    ok (subhash_subscribe (sub, "foo") == 0,
        "subhash_subscribe foo");
    ok (subhash_subscribe (sub, "foo") == 0,
        "subhash_subscribe foo (again)");

    ok (subhash_topic_match (sub, "foo") == true,
        "subhash_topic_match foo returns true");
    ok (subhash_topic_match (sub, "foo.bar") == true,
        "subhash_topic_match foo.bar returns true");
    ok (subhash_topic_match (sub, "foobar") == true,
        "subhash_topic_match foobar returns true");
    ok (subhash_topic_match (sub, "fo") == false,
        "subhash_topic_match fo returns false");
    ok (subhash_topic_match (sub, "bar") == false,
        "subhash_topic_match bar returns false");

    /* 1st unsubscribe decrements refcount */
    ok (subhash_unsubscribe (sub, "foo") == 0,
        "subhash_unsubscribe foo");

    ok (subhash_topic_match (sub, "foo") == true,
        "subhash_topic_match foo returns true");

    /* 2nd unsubscribe removes entry */
    ok (subhash_unsubscribe (sub, "foo") == 0,
        "subhash_unsubscribe foo (again)");

    ok (subhash_topic_match (sub, "foo") == false,
        "subhash_topic_match foo returns false");

    subhash_destroy (sub);
}

int counter_cb (const char *topic, void *arg)
{
    int *count = arg;
    (*count)++;
    return 0;
}

void test_callbacks (void)
{
    struct subhash *sub;
    int sub_count;
    int unsub_count;

    if (!(sub = subhash_create ()))
        BAIL_OUT ("subhash_create failed");

    subhash_set_subscribe (sub, counter_cb, &sub_count);
    subhash_set_unsubscribe (sub, counter_cb, &unsub_count);

    sub_count = 0;
    unsub_count = 0;

    /* only 1st subscribe triggers sub callback */
    ok (subhash_subscribe (sub, "foo") == 0,
        "subhash_subscribe foo");
    ok (sub_count == 1,
        "sub callback called once");
    ok (subhash_subscribe (sub, "foo") == 0,
        "subhash_subscribe foo (again)");
    ok (sub_count == 1,
        "sub callback not called");

    /* only last subscribe triggers unsub callback */
    ok (subhash_unsubscribe (sub, "foo") == 0,
        "subhash_unsubscribe foo");
    ok (unsub_count == 0,
        "unsub callback not called");
    ok (subhash_unsubscribe (sub, "foo") == 0,
        "subhash_unsubscribe foo (again)");
    ok (unsub_count == 1,
        "sub callback called once");

    sub_count = 0;
    unsub_count = 0;

    /* subhash destroy unsubscribes to remaining topics */
    ok (subhash_subscribe (sub, "bar") == 0,
        "subhash_subscribe bar");
    ok (subhash_subscribe (sub, "baz") == 0,
        "subhash_subscribe baz");
    ok (sub_count == 2,
        "sub callback called twice");

    subhash_destroy (sub);

    ok (unsub_count == 2,
        "unsub callback called twice on subhash_destroy");
}

int rc_cb (const char *topic, void *arg)
{
    int *rc = arg;
    return *rc;
}

void test_callbacks_rc (void)
{
    struct subhash *sub;
    int rc;

    if (!(sub = subhash_create ()))
        BAIL_OUT ("subhash_create failed");

    subhash_set_subscribe (sub, rc_cb, &rc);
    subhash_set_unsubscribe (sub, rc_cb, &rc);

    rc = -1;
    ok (subhash_subscribe (sub, "bar") < 0,
        "subhash_subscribe bar failed due to callback rc < 0");
    rc = 0;
    ok (subhash_subscribe (sub, "bar") == 0,
        "subhash_subscribe bar works due to callback rc == 0");
    rc = -1;
    ok (subhash_unsubscribe (sub, "bar") < 0,
        "subhash_unsubscribe bar fails due to callback rc < 0");
    rc = 0;
    ok (subhash_unsubscribe (sub, "bar") == 0,
        "subhash_unsubscribe bar works due to callback rc == 0");

    subhash_destroy (sub);
}

void test_errors (void)
{
    struct subhash *sub;

    if (!(sub = subhash_create ()))
        BAIL_OUT ("subhash_create failed");

    errno = 0;
    ok (subhash_unsubscribe (NULL, "foo") < 0 && errno == EINVAL,
        "subhash_unsubscribe sub=NULL fails with EINVAL");
    errno = 0;
    ok (subhash_unsubscribe (sub, NULL) < 0 && errno == EINVAL,
        "subhash_unsubscribe topic=NULL fails with EINVAL");
    errno = 0;
    ok (subhash_unsubscribe (sub, "bar") < 0 && errno == ENOENT,
        "subhash_unsubscribe topic=<unknown> fails with ENOENT");

    errno = 0;
    ok (subhash_subscribe (NULL, "foo") < 0 && errno == EINVAL,
        "subhash_subscribe sub=NULL fails with EINVAL");
    errno = 0;
    ok (subhash_subscribe (sub, NULL) < 0 && errno == EINVAL,
        "subhash_subscribe topic=NULL fails with EINVAL");

    ok (subhash_topic_match (NULL, "foo") == false,
        "subhash_topic_match sub=NULL returns false");
    ok (subhash_topic_match (sub, NULL) == false,
        "subhash_topic_match topic=NULL returns false");

    lives_ok ({ subhash_destroy (NULL);},
        "subhash_destroy sub=NULL doesn't crash");

    subhash_destroy (sub);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_topic_match ();
    test_callbacks ();
    test_callbacks_rc ();
    test_errors ();

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
