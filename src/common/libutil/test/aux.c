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
#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/aux.h"

struct aux_container {
    struct aux_item *aux;
    int count;
};

void c_cb (void *arg)
{
    struct aux_container *ac = arg;
    ac->count++;
}
void b_cb (void *arg)
{
    struct aux_container *ac = arg;
    ac->count++;
    aux_set (&ac->aux, "c", ac, c_cb);
}

void a_cb (void *arg)
{
    struct aux_container *ac = arg;
    ac->count++;
    aux_set (&ac->aux, "b", ac, b_cb);
}

void aux_destroy_set_ok (void)
{
    struct aux_container ac;
    ac.aux = NULL;
    ac.count = 0;

    aux_set (&ac.aux, "a", &ac, a_cb);
    aux_destroy (&ac.aux);
    ok (ac.count == 3,
        "aux_destroy allows list to be modified");
    diag ("ac.count=%d", ac.count);
}

void ngs_free (void *arg)
{
    struct aux_container *ac = arg;
    if (aux_get (ac->aux, "foo"))
        ac->count++;
    if (aux_get (ac->aux, "bar"))
        ac->count++;
    if (aux_get (ac->aux, "baz"))
        ac->count++;
}

/* aux_destroy iteration should not allow aux_get to succeed on
 * the item being destroyed
 */
void aux_destroy_no_get_self (void)
{
    struct aux_container ac;
    ac.aux = NULL;
    ac.count = 0;

    aux_set (&ac.aux, "foo", &ac, ngs_free);
    aux_set (&ac.aux, "bar", &ac, ngs_free);
    aux_set (&ac.aux, "baz", &ac, ngs_free);
    aux_destroy (&ac.aux);

    /* 2+1+0 == 3 */
    ok (ac.count == 3,
        "aux_destroy doesn't allow access to items being destroyed");
}

int myfree_count;
void myfree (void *arg)
{
    myfree_count++;
}

void simple_test (void)
{
    struct aux_item *aux = NULL;

    errno = 0;
    ok (aux_get (aux, "frog") == NULL && errno == ENOENT,
        "aux_get fails with ENOENT on unknown item");

    /* set 1st item no destructor */
    ok (aux_set (&aux, "frog", "ribbit", NULL) == 0,
        "aux_set frog=ribbit free_fn=NULL works");
    is (aux_get (aux, "frog"), "ribbit",
        "aux_get frog returns ribbit");

    /* set 2nd item with destructor */
    ok (aux_set (&aux, "dog", "woof", myfree) == 0,
        "aux_set dog=woof free_fn=myfree");
    is (aux_get (aux, "dog"), "woof",
        "aux_get dog returns woof");
    is (aux_get (aux, "frog"), "ribbit",
        "aux_get frog still returns ribbit");

    /* set 3rd item with destructor */
    ok (aux_set (&aux, "cow", "moo", myfree) == 0,
        "aux_set cow=moo free_fn=myfree");
    is (aux_get (aux, "cow"), "moo",
        "aux_get cow returns moo");
    is (aux_get (aux, "dog"), "woof",
        "aux_get dog still returns woof");
    is (aux_get (aux, "frog"), "ribbit",
        "aux_get frog still returns ribbit");

    /* aux_set duplicate */
    myfree_count = 0;
    ok (aux_set (&aux, "cow", "oink", myfree) == 0,
        "aux_set cow=oink free_fn=myfree");
    cmp_ok (myfree_count, "==", 1,
        "dup key=cow triggered destructor");
    is (aux_get (aux, "cow"), "oink",
        "aux_get cow now returns oink");

    /* aux_set val=NULL */
    myfree_count = 0;
    ok (aux_set (&aux, "cow", NULL, NULL) == 0,
        "aux_set cow=NULL does not fail");
    cmp_ok (myfree_count, "==", 1,
        "and called destructor once");
    errno = 0;
    ok (aux_get (aux, "cow") == NULL && errno == ENOENT,
        "aux_get cow fails with ENOENT");
    ok (aux_set (&aux, "unknown-key", NULL, NULL) == 0,
        "aux_set unknown-key=NULL does not fail");

    /* invalid args */
    errno = 0;
    ok (aux_get (aux, NULL) == NULL && errno == EINVAL,
        "aux_get key=NULL fails with EINVAL");
    errno = 0;
    ok (aux_set (&aux, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "aux_set key=NULL val=NULL fails with EINVAL");
    errno = 0;
    ok (aux_set (NULL, "frog", NULL, NULL) < 0 && errno == EINVAL,
        "aux_set aux=NULL fails with EINVAL");
    errno = 0;
    ok (aux_set (&aux, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "aux_set key=NULL val=NULL fails with EINVAL");
    errno = 0;
    ok (aux_set (&aux, "frog", NULL, myfree) < 0 && errno == EINVAL,
        "aux_set val=NULL free_fn!=NULL fails with EINVAL");
    errno = 0;
    ok (aux_set (&aux, NULL, "baz", NULL) < 0 && errno == EINVAL,
        "aux_set key=NULL free_fn=NULL fails with EINVAL");

    /* add anonymous item */
    ok (aux_set (&aux, NULL, "foo", myfree) == 0,
        "aux-set key=NULL works for anonymous items");

    /* destroy */
    myfree_count = 0;
    aux_destroy (&aux);
    cmp_ok (myfree_count, "==", 2,
        "aux_destroy called myfree twice");
    ok (aux == NULL,
        "aux_destroy set aux to NULL");

    lives_ok ({aux_destroy (NULL);},
        "aux_destroy aux=NULL doesn't crash");
}

void test_delete (void)
{
    struct aux_item *aux = NULL;
    int items[8];
    int i;

    for (i = 0; i < 8; i++)
        if (aux_set (&aux, NULL, &items[i], myfree) < 0)
            BAIL_OUT ("aux_set failed on item %d", i);

    myfree_count = 0;
    aux_delete_value (NULL, "foo");
    ok (myfree_count == 0,
        "aux_delete_value aux=NULL does nothing");

    myfree_count = 0;
    aux_delete_value (&aux, NULL);
    ok (myfree_count == 0,
        "aux_delete_value val=NULL does nothing");

    myfree_count = 0;
    aux_delete_value (&aux, &i);
    ok (myfree_count == 0,
        "aux_delete_value val=unknown does nothing");

    myfree_count = 0;
    for (i = 0; i < 8; i++)
        aux_delete_value (&aux, &items[i]);
    ok (myfree_count == 8,
        "aux_delete_value works with valid pointer");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    simple_test ();
    aux_destroy_no_get_self ();
    aux_destroy_set_ok ();
    test_delete ();

    done_testing ();

    return 0;
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
