/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/aux.h"

int myfree_count;
void myfree (void *arg)
{
    myfree_count++;
}

int main (int argc, char *argv[])
{
    struct aux_item *aux = NULL;

    plan (NO_PLAN);

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

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
