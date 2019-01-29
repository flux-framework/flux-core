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

#include <jansson.h>
#include <string.h>
#include <errno.h>

#include "kvs.h"
#include "kvs_txn_private.h"
#include "kvs_dir_private.h"
#include "treeobj.h"

#include "src/common/libflux/flux.h"
#include "kvs_dir.h"
#include "src/common/libtap/tap.h"

void jdiag (json_t *o)
{
    char *tmp = json_dumps (o, JSON_COMPACT);
    diag ("%s", tmp);
    free (tmp);
}

void test_empty (void)
{
    flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    json_t *o;
    char *s;
    const char *key;

    lives_ok ({flux_kvsdir_destroy (NULL);},
        "flux_kvsdir_destroy with NULL paramter doesn't crash");

    errno = 0;
    dir = flux_kvsdir_create (NULL, NULL, NULL, NULL);
    ok (dir == NULL && errno == EINVAL,
        "flux_kvsdir_create with all NULL args fails with EINVAL");

    errno = 0;
    dir = flux_kvsdir_create (NULL, NULL, "foo", "{}");
    ok (dir == NULL && errno == EINVAL,
        "flux_kvsdir_create with empty JSON objects fails with EINVAL");

    errno = 0;
    dir = flux_kvsdir_create (NULL, NULL, "foo", "foo");
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with bad JSON objects fails with EINVAL");

    errno = 0;
    dir = flux_kvsdir_create (NULL, NULL, "foo",
                         "{\"data\":\"MQA=\",\"type\":\"FOO\",\"ver\":1}");
    ok (dir == NULL && errno == EINVAL,
        "flux_kvsdir_create with invalid treeobj fails with EINVAL");

    errno = 0;
    dir = flux_kvsdir_create (NULL, NULL, "foo",
                         "{\"data\":\"MQA=\",\"type\":\"val\",\"ver\":1}");
    ok (dir == NULL && errno == EINVAL,
        "flux_kvsdir_create with non-dir treeobj fails with EINVAL");

    if (!(o = treeobj_create_dir ()))
        BAIL_OUT ("treeobj_create_dir failed");
    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed on new treeobj");
    dir = flux_kvsdir_create (NULL, NULL, "foo", s);
    free (s);

    ok (dir != NULL,
        "flux_kvsdir_create with empty directory works");
    jdiag (kvsdir_get_obj (dir));

    ok (!flux_kvsdir_exists (dir, "noexist"),
        "flux_kvsdir_exists on nonexistent key returns false");
    ok (!flux_kvsdir_isdir (dir, "noexist"),
        "flux_kvsdir_isdir on nonexistent key returns false");
    ok (!flux_kvsdir_issymlink (dir, "noexist"),
        "flux_kvsdir_issymlink on nonexistent key returns false");

    key = flux_kvsdir_key (dir);
    ok (key != NULL && !strcmp (key, "foo"),
        "flux_kvsdir_key returns the key we put in");
    key = flux_kvsdir_key_at (dir, "a.b.c");
    ok (key != NULL && !strcmp (key, "foo.a.b.c"),
        "flux_kvsdir_key_at a.b.c returns foo.a.b.c");
    ok (flux_kvsdir_handle (dir) == NULL,
        "flux_kvsdir_handle returns NULL since that's what we put in");
    ok (flux_kvsdir_rootref (dir) == NULL,
        "flux_kvsdir_rootref returns NULL since that's what we put in");
    ok (flux_kvsdir_get_size (dir) == 0,
        "flux_kvsdir_get_size returns zero");

    errno = 0;
    ok (flux_kvsitr_create (NULL) == NULL && errno == EINVAL,
        "flux_kvsitr_create with NULL dir fails with EINVAL");
    ok (flux_kvsitr_next (NULL) == NULL,
        "flux_kvsitr_next on NULL iterator returns NULL");
    lives_ok ({flux_kvsitr_rewind (NULL);},
        "flux_kvsitr_rewind on NULL iterator doesn't crash");
    lives_ok ({flux_kvsitr_destroy (NULL);},
        "flux_kvsitr_destroy on NULL iterator doesn't crash");

    itr = flux_kvsitr_create (dir);
    ok (itr != NULL,
        "flux_kvsitr_create works");
    ok (flux_kvsitr_next (itr) == NULL,
        "flux_kvsitr_next returns NULL on first call");
    ok (flux_kvsitr_next (itr) == NULL,
        "flux_kvsitr_next returns NULL on second call");
    flux_kvsitr_rewind (itr);
    ok (flux_kvsitr_next (itr) == NULL,
        "flux_kvsitr_next returns NULL after rewind");
    flux_kvsitr_destroy (itr);

    flux_kvsdir_destroy (dir);
}

void test_full (void)
{
    flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    json_t *o, *dirent;
    char *s;

    if (!(o = treeobj_create_dir ()))
        BAIL_OUT ("treeobj_create_dir failed");
    if (!(dirent = treeobj_create_symlink (NULL, "a.b.c")))
        BAIL_OUT ("treeobj_create_symlink failed (no namespace)");
    if (treeobj_insert_entry (o, "foo", dirent) < 0)
        BAIL_OUT ("treeobj_insert_entry failed");
    json_decref (dirent);
    if (!(dirent = treeobj_create_val ("xxxx", 4)))
        BAIL_OUT ("treeobj_create_val failed");
    if (treeobj_insert_entry (o, "bar", dirent) < 0)
        BAIL_OUT ("treeobj_insert_entry failed");
    json_decref (dirent);
    if (!(dirent = treeobj_create_dir ()))
        BAIL_OUT ("treeobj_create_dir failed");
    if (treeobj_insert_entry (o, "baz", dirent) < 0)
        BAIL_OUT ("treeobj_insert_entry failed");
    json_decref (dirent);
    if (!(dirent = treeobj_create_symlink ("ns", "d.e.f")))
        BAIL_OUT ("treeobj_create_symlink failed (namespace)");
    if (treeobj_insert_entry (o, "boo", dirent) < 0)
        BAIL_OUT ("treeobj_insert_entry failed");
    json_decref (dirent);

    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed on new treeobj");
    dir = flux_kvsdir_create (NULL, NULL, "foo", s);
    free (s);
    ok (dir != NULL,
        "flux_kvsdir_create works");
    jdiag (kvsdir_get_obj (dir));

    ok (!flux_kvsdir_exists (dir, "noexist"),
        "flux_kvsdir_exists on nonexistent key returns false");
    ok (flux_kvsdir_exists (dir, "foo"),
        "flux_kvsdir_exists on existing symlink returns true");
    ok (flux_kvsdir_exists (dir, "bar"),
        "flux_kvsdir_exists on existing val returns true");
    ok (flux_kvsdir_exists (dir, "baz"),
        "flux_kvsdir_exists on existing dir returns true");
    ok (flux_kvsdir_exists (dir, "boo"),
        "flux_kvsdir_exists on existing dir returns true");

    ok (!flux_kvsdir_isdir (dir, "noexist"),
        "flux_kvsdir_isdir on nonexistent key returns false");
    ok (!flux_kvsdir_isdir (dir, "foo"),
        "flux_kvsdir_isdir on existing symlink returns false");
    ok (!flux_kvsdir_isdir (dir, "bar"),
        "flux_kvsdir_isdir on existing val returns false");
    ok (flux_kvsdir_isdir (dir, "baz"),
        "flux_kvsdir_isdir on existing symlink returns true");
    ok (!flux_kvsdir_isdir (dir, "boo"),
        "flux_kvsdir_isdir on existing symlink returns false");

    ok (!flux_kvsdir_issymlink (dir, "noexist"),
        "flux_kvsdir_issymlink on nonexistent key returns false");
    ok (flux_kvsdir_issymlink (dir, "foo"),
        "flux_kvsdir_issymlink on existing symlink returns true");
    ok (!flux_kvsdir_issymlink (dir, "bar"),
        "flux_kvsdir_issymlink on existing val returns false");
    ok (!flux_kvsdir_issymlink (dir, "baz"),
        "flux_kvsdir_issymlink on existing dir returns false");
    ok (flux_kvsdir_issymlink (dir, "boo"),
        "flux_kvsdir_issymlink on existing dir returns false");

    ok (flux_kvsdir_get_size (dir) == 4,
        "flux_kvsdir_get_size returns 4");

    itr = flux_kvsitr_create (dir);
    ok (itr != NULL,
        "flux_kvsitr_create works");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL on first call");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL on second call");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL on third call");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns NULL on fourth call");
    ok (flux_kvsitr_next (itr) == NULL,
        "flux_kvsitr_next returns NULL on fifth call");

    flux_kvsitr_rewind (itr);
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL after rewind");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL on second call");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns non-NULL on third call");
    ok (flux_kvsitr_next (itr) != NULL,
        "flux_kvsitr_next returns NULL on fourth call");
    ok (flux_kvsitr_next (itr) == NULL,
        "flux_kvsitr_next returns NULL on fifth call");
    flux_kvsitr_destroy (itr);

    flux_kvsdir_t *cpy = flux_kvsdir_copy (dir);
    ok (cpy != NULL,
        "flux_kvsdir_copy was successful");
    ok (flux_kvsdir_get_size (cpy) == 4,
        "flux_kvsdir_get_size on copy returns 4");

    flux_kvsdir_destroy (dir);

    ok (flux_kvsdir_get_size (cpy) == 4,
        "flux_kvsdir_get_size on copy still returns 4 after orig freed");

    flux_kvsdir_destroy (cpy);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_empty ();
    test_full ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

