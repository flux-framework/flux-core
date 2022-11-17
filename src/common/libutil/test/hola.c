/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>

#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libutil/hola.h"

void key_destructor (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}
void *key_duplicator (const void *item)
{
    return strdup (item);
}
int key_comparator (const void *item1, const void *item2)
{
    return strcmp (item1, item2);
}
size_t
key_hasher (const void *key)
{
    const char *cp = key;
    size_t hash = 0;
    while (*cp)
        hash = 33 * hash ^ *cp++;
    return hash;
}

void test_hash (void)
{
    struct hola *h;
    const char *key;
    zlistx_t *l;

    ok ((h = hola_create (0)) != NULL,
        "hola_create works");

    /* set callbacks identical to internal zhashx defaults
     * just to cover functions
     */
    hola_set_hash_key_destructor (h, key_destructor);
    hola_set_hash_key_duplicator (h, key_duplicator);
    hola_set_hash_key_comparator (h, key_comparator);
    hola_set_hash_key_hasher (h, key_hasher);

    /* empty hash */
    ok (hola_hash_size (h) == 0,
        "hola_hash_size is 0");
    key = hola_hash_first (h);
    ok (key == NULL,
        "hola_hash_first returns NULL");
    key = hola_hash_next (h);
    ok (key == NULL,
        "hola_hash_next returns NULL");
    errno = 0;
    l = hola_hash_lookup (h, "foo");
    ok (l == NULL && errno == ENOENT,
        "hola_hash_lookup key=foo fails with ENOENT");
    errno = 0;
    ok (hola_hash_delete (h, "foo") < 0 && errno == ENOENT,
        "hola_hash_delete key=foo fails with ENOENT");

    /* one item */
    ok (hola_hash_add (h, "item1") == 0,
        "hola_hash_add key=item1 works");
    ok (hola_hash_size (h) == 1,
        "hola_hash_size is 1");
    errno = 0;
    ok (hola_hash_add (h, "item1") < 0
        && errno == EEXIST,
        "hola_hash_add key=item1 fails with EEXIST");
    key = hola_hash_first (h);
    ok (key && streq (key, "item1"),
        "hola_hash_first returns item1");
    key = hola_hash_next (h);
    ok (key == NULL,
        "hola_hash_next returns NULL");
    l = hola_hash_lookup (h, "item1");
    ok (l != NULL,
        "hola_hash_lookup key=item1 works");
    ok (hola_hash_delete (h, "item1") == 0
        && hola_hash_size (h) == 0,
        "hola_hash_delete key=item1 works");

    /* two items */
    ok (hola_hash_add (h, "item1") == 0,
        "hola_hash_add key=item1 works");
    ok (hola_hash_add (h, "item2") == 0,
        "hola_hash_add key=item2 works");
    ok (hola_hash_size (h) == 2,
        "hola_hash_size is 2");
    key = hola_hash_first (h);
    ok (key && (streq (key, "item1") || streq (key, "item2")),
        "hola_hash_first returns a valid key");
    key = hola_hash_next (h);
    ok (key && (streq (key, "item1") || streq (key, "item2")),
        "hola_hash_next returns a valid key");
    key = hola_hash_next (h);
    ok (key == NULL,
        "hola_hash_next returns NULL");
    l = hola_hash_lookup (h, "item1");
    ok (l != NULL,
        "hola_hash_lookup key=item1 works");
    l = hola_hash_lookup (h, "item2");
    ok (l != NULL,
        "hola_hash_lookup key=item2 works");
    ok (hola_hash_delete (h, "item1") == 0
        && hola_hash_size (h) == 1,
        "hola_hash_delete key=item1 works");
    key = hola_hash_first (h);
    ok (key && streq (key, "item2"),
        "hola_hash_first returns item2");
    key = hola_hash_next (h);
    ok (key == NULL,
        "hola_hash_next returns NULL");

    hola_destroy (h);
}

void test_auto (void)
{
    struct hola *h;
    void *item1;
    void *item2;
    void *item3;

    ok ((h = hola_create (HOLA_AUTOCREATE | HOLA_AUTODESTROY)) != NULL,
        "hola_create AUTOCREATE | AUTODDESTROY works");

    item1 = hola_list_add_end (h, "blue", "item1");
    ok (item1 != NULL,
        "hola_add_end key=blue value=item1 works");
    item2 = hola_list_add_end (h, "red", "item2");
    ok (item2 != NULL,
        "hola_add_end key=red value=item2 works");
    item3 = hola_list_add_end (h, "red", "item3");
    ok (item3 != NULL,
        "hola_add_end key=red value=item3 works");
    ok (hola_hash_size (h) == 2,
        "hola_hash_size is 2");
    ok (hola_list_size (h, "blue") == 1,
        "hola_list_size is key=blue is 1");
    ok (hola_list_size (h, "red") == 2,
        "hola_list_size is key=red is 2");
    ok (hola_list_delete (h, "red", item3) == 0
        && hola_list_size (h, "red") == 1,
        "hola_list_delete key=red item3 works");
    ok (hola_list_delete (h, "red", item2) == 0
        && hola_list_size (h, "red") == 0,
        "hola_list_delete key=red item3 works");
    ok (hola_hash_size (h) == 1,
        "hola_hash_size is 1");
    ok (hola_list_delete (h, "blue", item1) == 0
        && hola_list_size (h, "blue") == 0,
        "hola_list_delete key=blue item1 works");
    ok (hola_hash_size (h) == 0,
        "hola_hash_size is 0");

    hola_destroy (h);
}

struct test_input {
    const char *key;
    char *val;
};

struct test_input test1[] = {
    { "blue", "item1" },
    { "blue", "item2" },
    { "blue", "item3" },
    { "red", "item4" },
    { "red", "item5" },
    { "green", "item6" },
};

int find_entry (struct test_input *t,
                size_t len,
                const char *key,
                const char *val)
{
    if (key && val) {
        for (int i = 0; i < len; i++) {
            if (streq (key, t[i].key) && streq (val, t[i].val))
                return i;
        }
    }
    return -1;
}

bool test_iter_one (struct test_input *t, size_t len)
{
    struct hola *h;
    const char *key;
    const char *val;
    bool *checklist;
    bool result = true;

    if (!(checklist = calloc (len, sizeof (checklist[0]))))
        BAIL_OUT ("out of memory");
    h = hola_create (HOLA_AUTOCREATE);
    if (!h)
        BAIL_OUT ("hola_create failed");
    for (int i = 0; i < len; i++) {
        if (!hola_list_add_end (h, t[i].key, t[i].val))
            BAIL_OUT ("could not populate test object for iteration");
    }
    key = hola_hash_first (h);
    while (key) {
        val = hola_list_first (h, key);
        while (val) {
            int index = find_entry (t, len, key, val);
            if (hola_list_cursor (h, key) == NULL) // cursor must not be NULL
                break;
            if (index != -1)
                checklist[index] = true;
            val = hola_list_next (h, key);
        }
        if (hola_list_cursor (h, key) != NULL) // cursor must be NULL
            break;

        key = hola_hash_next (h);
    }
    for (int i = 0; i < len; i++)
        if (!checklist[i])
            result = false;

    hola_destroy (h);
    free (checklist);
    return result;
}

void test_iter (void)
{
    ok (test_iter_one (test1, ARRAY_SIZE (test1)) == true,
        "iteration works");
}

void test_inval (void)
{
    struct hola *h;
    if (!(h = hola_create (0)))
        BAIL_OUT ("hola_create failed");

    errno = 0;
    ok (hola_create (0xff) == NULL && errno == EINVAL,
        "hola_create flags=0xff fails with EINVAL");

    errno = 0;
    ok (hola_hash_lookup (NULL, "foo") == NULL && errno == EINVAL,
        "hola_hash_lookup h=NULL fails with EINVAL");

    errno = 0;
    ok (hola_hash_lookup (h, NULL) == NULL && errno == EINVAL,
        "hola_hash_lookup key=NULL fails with EINVAL");

    errno = 0;
    ok (hola_hash_add (NULL, "foo") < 0 && errno == EINVAL,
        "hola_hash_add h=NULL fails with EINVAL");

    errno = 0;
    ok (hola_hash_add (h, NULL) < 0 && errno == EINVAL,
        "hola_hash_add key=NULL fails with EINVAL");

    errno = 0;
    ok (hola_hash_delete (NULL, "foo") < 0 && errno == EINVAL,
        "hola_hash_delete h=NULL fails with EINVAL");

    errno = 0;
    ok (hola_hash_delete (h, NULL) < 0 && errno == EINVAL,
        "hola_hash_delete key=NULL fails with EINVAL");

    ok (hola_hash_first (NULL) == NULL,
        "hola_hash_first h=NULL returns NULL");
    ok (hola_hash_next (NULL) == NULL,
        "hola_hash_next h=NULL returns NULL");
    ok (hola_hash_size (NULL) == 0,
        "hola_hash_size h=NULL returns 0");

    errno = 0;
    ok (hola_list_add_end (NULL, "foo", "foo") == NULL && errno == EINVAL,
        "hola_list_add_end h=NULL fails with EINVAL");

    errno = 0;
    ok (hola_list_add_end (h, NULL, "foo") == NULL && errno == EINVAL,
        "hola_list_add_end key=NULL fails with EINVAL");

    errno = 0;
    ok (hola_list_add_end (h, "foo", NULL) == NULL && errno == EINVAL,
        "hola_list_add_end item=NULL fails with EINVAL");

    errno = 0;
    ok (hola_list_add_end (h, "noexist", "bar") == NULL && errno == ENOENT,
        "hola_list_add_end key=nonexistent list fails with ENOENT");

    errno = 0;
    ok (hola_list_delete (NULL, "foo", "bar") < 0 && errno == EINVAL,
        "hola_list_delete h=NULL fails with EINVAL");

    errno = 0;
    ok (hola_list_delete (h, NULL, "bar") < 0 && errno == EINVAL,
        "hola_list_delete key=NULL fails with EINVAL");
    errno = 0;
    ok (hola_list_delete (h, "foo", NULL) < 0 && errno == EINVAL,
        "hola_list_delete handle=NULL fails with EINVAL");
    errno = 0;
    ok (hola_list_delete (h, "foo", &errno) < 0 && errno == ENOENT,
        "hola_list_delete key=unknown fails with ENOENT");

    ok (hola_list_first (NULL, "foo") == NULL,
        "hola_list_first h=NULL returns NULL");
    ok (hola_list_first (h, NULL) == NULL,
        "hola_list_first key=NULL returns NULL");
    ok (hola_list_next (NULL, "foo") == NULL,
        "hola_list_next h=NULL returns NULL");
    ok (hola_list_next (h, NULL) == NULL,
        "hola_list_next key=NULL returns NULL");
    ok (hola_list_cursor (NULL, "foo") == NULL,
        "hola_list_cursor h=NULL returns NULL");
    ok (hola_list_cursor (h, NULL) == NULL,
        "hola_list_cursor key=NULL returns NULL");

    lives_ok ({hola_set_hash_key_destructor (NULL, key_destructor);},
        "holas_set_hash_key_destructor h=NULL doesn't crash");
    lives_ok ({hola_set_hash_key_duplicator (NULL, key_duplicator);},
        "holas_set_hash_key_duplicator h=NULL doesn't crash");
    lives_ok ({hola_set_hash_key_comparator (NULL, key_comparator);},
        "holas_set_hash_key_comparator h=NULL doesn't crash");
    lives_ok ({hola_set_hash_key_hasher (NULL, key_hasher);},
        "holas_set_hash_key_hasher h=NULL doesn't crash");

    lives_ok ({hola_destroy (NULL);},
        "hola_destroy h=NULL doesn't crash");

    hola_destroy (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_hash ();
    test_auto ();
    test_iter ();
    test_inval ();

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
