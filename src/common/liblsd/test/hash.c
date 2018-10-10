/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "hash.h"

#include "src/common/libtap/tap.h"


/* Dummy comparison for void * */
static int cmpf (const void *x, const void *y)
{
    if (x < y)
        return (-1);
    if (x > y)
        return (1);
    return (0);
}

static void sanity_checks ()
{
    void *arg;
    void *x;
    hash_t h;

    ok (hash_create (0, NULL, NULL, NULL) == NULL && errno == EINVAL,
        "hash_create with NULL cmp_f and key_f fails with EINVAL");

    h = hash_create (0, (hash_key_f) hash_key_string, cmpf, NULL);

    ok (h != NULL, "hash_create (0, NULL, NULL, NULL) h == %p", h);
    ok (hash_is_empty (h), "hash_is_empty ()");
    ok (hash_count (h) == 0, "hash_count () == %d", hash_count (h));

    errno = 0;
    ok (hash_count (NULL) == 0 && errno == EINVAL,
        "hash_count on NULL hash returns 0 with errno set");
    /*  A NULL hash has count == 0, but it's not empty! ;-)
     */
    errno = 0;
    ok (hash_is_empty (NULL) == 0 && errno == EINVAL,
        "hash_is_empty on NULL hash returns 0 with errno set");

    ok (hash_insert (NULL, "foo", (void *) 0x1) == NULL && errno == EINVAL,
        "hash_insert to NULL hash fails with EINVAL");
    ok (hash_insert (h, "foo", NULL) == NULL && errno == EINVAL,
        "hash_insert of NULL fails with EINVAL");
    ok (hash_insert (h, NULL, (void *) 0xff) == NULL && errno == EINVAL,
        "hash_insert of NULL key fails with EINVAL");

    ok (NULL != hash_insert (h, "foo", (void *) 0xafafaf), "hash_insert works");
    ok (hash_is_empty (h) == 0, "hash_is_empty() == 0");
    ok (hash_count (h) == 1, "hash_count() == 1");

    ok (hash_insert (h, "foo", (void *) 0x1) == NULL && errno == EEXIST,
        "hash_insert of duplicate key fails with EEXIST");

    ok ((arg = hash_find (h, NULL)) == NULL && errno == EINVAL,
        "hash_find of NULL key returns NULL with errno == EINVAL");
    ok ((arg = hash_find (NULL, "foo")) == NULL && errno == EINVAL,
        "hash_find on NULL hash returns NULL with errno == EINVAL");

    ok ((arg = hash_find (h, "foo")) != NULL, "hash_find: works");
    ok (arg == (void *) 0xafafaf, "hash_find: returned data is correct");

    ok (hash_delete_if (h, NULL, NULL) == -1 && errno == EINVAL,
        "hash_delete_if returns -1 with errno == EINVAL for invalid argf");

    errno = 0;
    hash_reset (NULL);
    ok (errno == EINVAL, "hash_reset on NULL hash sets errno == EINVAL");

    ok (hash_for_each (h, NULL, NULL) == -1 && errno == EINVAL,
        "hash_for_each returns -1 with errno = EINVAL on invalid argf");
    ok (hash_for_each (NULL, NULL, NULL) == -1 && errno == EINVAL,
        "hash_for_each returns -1 with errno = EINVAL on NULL hash");

    ok (hash_remove (NULL, "foo") == NULL && errno == EINVAL,
        "hash_remove of NULL hash fails with EINVAL");
    ok (hash_remove (h, NULL) == NULL && errno == EINVAL,
        "hash_remove of NULL key fails with EINVAL");

    ok ((x = hash_remove (h, "foo")) != NULL, "hash_remove: works");
    ok (x == arg, "hash_remove: returned item's data on success");
    ok (hash_count (h) == 0, "hash_count is zero after removal");
    ok (hash_is_empty (h), "hash is empty after removal");

    hash_destroy (NULL);
    ok (errno == EINVAL, "hash_destroy of NULL hash sets errno to EINVAL");

    hash_destroy (h);
}

static int foreach (void *data, const void *key, void *arg)
{
    if (!key || !data)
        return (0);
    return (1);
}

/* XXX: this list of keys needs to stay global because hash doesn't
 *      copy keys, and I'm being too lazy to create a container for
 *      them and manual manage the memory for a test program.
 */
static const char *k[] = {
    "foo", "bar", "baz", "bloop", "bleep", "blurg", NULL
};

/*  Create fake data for hash item from integer value */
static void * fake_data (int value)
{
    return (void *)(0xfL + (unsigned long) value);
}

static void hash_calisthenics (const char *prefix, hash_t h)
{
    int i;
    int klen = (sizeof (k) / sizeof (k[0])) - 1;

    ok (hash_for_each (h, (hash_arg_f) foreach, NULL) == hash_count (h),
        "%s: hash_for_each works", prefix);

    for (i = 0; i < klen; i++) {
        void *x = hash_find (h, k[i]);
        ok (x != NULL,
            "%s: hash_find ('%s') works x=%p", prefix, k[i], x);
        ok (x == fake_data (i),
            "%s: hash_find found expected value", prefix);
    }

    ok ((hash_remove (h, k[1]) == fake_data (1)),
        "%s: hash_remove of single item works", prefix);
    ok (hash_count (h) == klen-1,
        "%s: hash_count is reduced by 1", prefix);

    hash_reset (h);

    ok (hash_count (h) == 0,
        "%s: hash_count is zero after reset", prefix);
    ok (hash_is_empty (h),
        "%s: hash is empty after reset", prefix);
}

static hash_t do_hash_create (int size, hash_del_f fn)
{
    int i;
    int klen = (sizeof (k) / sizeof (k[0])) - 1;
    hash_t h = hash_create (size, (hash_key_f) hash_key_string, cmpf, fn);
    ok (h != NULL, "hash_create (size = %d)", size);
    if (h == NULL)
        return NULL;

    for (i = 0; i < klen; i++) {
        void *result = hash_insert (h, k[i], fake_data (i));
        if (result == NULL)
            fail ("size=%d: hash_insert (%s) failed", size, k[i]);
    }

    ok (hash_count (h) == klen,
        "size=%d: Successfully inserted %d hash entries", size, klen);
    return (h);
}


static void test_for_each ()
{
    hash_t h = do_hash_create (0, NULL);
    if (!h)
        return;
    hash_calisthenics ("default", h);
    hash_destroy (h);
}

static void test_chaining ()
{
    /* Force chaining via hash size of 1 */
    hash_t h = do_hash_create (1, NULL);
    if (!h)
        return;
    hash_calisthenics ("chaining", h);
    hash_destroy (h);
}

static int delete_count = 0;
static void del_f (void *data)
{
    delete_count++;
}

static int cmp_key (void *data, const void *key, void *arg)
{
    return (strcmp ((const char *) key, (const char *) arg) == 0);
}

static void test_delete ()
{
    int size;

    /*  Try with default size and size=1 to force chaining */
    for (size = 0; size <= 1; size++) {
        int count;
        hash_t h = do_hash_create (size, del_f);
        ok (h != NULL, "size %d: hash_create", size);
        if (h == NULL)
            return;
        count = hash_count (h);
        delete_count = 0;
        hash_destroy (h);
        ok (delete_count == count,
            "size %d: hash_destroy() deleted all items", size);

        /*  Execute same test with hash_reset()  */
        h = do_hash_create (size, del_f);
        ok (h != NULL, "size %d: hash_create", size);
        if (h == NULL)
            return;
        count = hash_count (h);
        delete_count = 0;
        hash_reset (h);
        ok (delete_count == count,
            "size %d: hash_reset() deleted all items", size);
        ok (hash_is_empty (h), "size %d: hash is empty after reset");

        delete_count = 0;
        hash_destroy (h);
        ok (delete_count == 0, "size %d: no items deleted from empty hash");

        /*  Test hash_delete_if */
        h = do_hash_create (size, del_f);
        ok (h != NULL, "size %d: hash_create", size);
        if (h == NULL)
            return;
        count = hash_count (h);
        delete_count = 0;
        ok (1 == hash_delete_if (h, cmp_key, "bleep"),
            "size %d: hash_delete_if works", size);
        ok (delete_count == 1,
            "size %d: hash_delete_if destroyed 1 item", size);
        ok (0 == hash_delete_if (h, cmp_key, "bleep"),
            "size %d: hash_delete_if returns 0 for no matches", size);
        ok (hash_count (h) == count - 1,
            "size %d: hash_count reduced by 1");
        delete_count = 0;
        hash_destroy (h);
        ok (delete_count == count - 1,
            "size %d: remaining items freed by hash_destroy");
    }
}


int
main (int argc, char *argv[])
{
    plan (NO_PLAN);

    sanity_checks ();
    test_for_each ();
    test_chaining ();
    test_delete ();

    hash_drop_memory ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
