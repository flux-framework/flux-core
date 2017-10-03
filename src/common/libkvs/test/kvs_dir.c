#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <string.h>
#include <errno.h>

#include "kvs.h"
#include "kvs_txn_private.h"
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
    kvsitr_t *itr;
    json_t *o;
    char *s;
    const char *key;

    errno = 0;
    dir = kvsdir_create (NULL, NULL, NULL, NULL);
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with all NULL args fails with EINVAL");

    errno = 0;
    dir = kvsdir_create (NULL, NULL, "foo", "{}");
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with empty JSON objects fails with EINVAL");

    errno = 0;
    dir = kvsdir_create (NULL, NULL, "foo", "foo");
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with bad JSON objects fails with EINVAL");

    errno = 0;
    dir = kvsdir_create (NULL, NULL, "foo",
                         "{\"data\":\"MQA=\",\"type\":\"FOO\",\"ver\":1}");
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with invalid treeobj fails with EINVAL");

    errno = 0;
    dir = kvsdir_create (NULL, NULL, "foo",
                         "{\"data\":\"MQA=\",\"type\":\"val\",\"ver\":1}");
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with non-dir treeobj fails with EINVAL");

    if (!(o = treeobj_create_dir ()))
        BAIL_OUT ("treeobj_create_dir failed");
    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed on new treeobj");
    dir = kvsdir_create (NULL, NULL, "foo", s);
    free (s);

    ok (dir != NULL,
        "kvsdir_create with empty directory works");
    diag ("%s", kvsdir_tostring (dir));

    ok (!kvsdir_exists (dir, "noexist"),
        "kvsdir_exists on nonexistent key returns false");
    ok (!kvsdir_isdir (dir, "noexist"),
        "kvsdir_isdir on nonexistent key returns false");
    ok (!kvsdir_issymlink (dir, "noexist"),
        "kvsdir_issymlink on nonexistent key returns false");

    key = kvsdir_key (dir);
    ok (key != NULL && !strcmp (key, "foo"),
        "kvsdir_key returns the key we put in");
    key = kvsdir_key_at (dir, "a.b.c");
    ok (key != NULL && !strcmp (key, "foo.a.b.c"),
        "kvsdir_key_at a.b.c returns foo.a.b.c");
    ok (kvsdir_handle (dir) == NULL,
        "kvsdir_handle returns NULL since that's what we put in");
    ok (kvsdir_rootref (dir) == NULL,
        "kvsdir_rootref returns NULL since that's what we put in");
    ok (kvsdir_get_size (dir) == 0,
        "kvsdir_get_size returns zero");

    errno = 0;
    ok (kvsitr_create (NULL) == NULL && errno == EINVAL,
        "kvsitr_create with NULL dir fails with EINVAL");
    ok (kvsitr_next (NULL) == NULL,
        "kvsitr_next on NULL iterator returns NULL");
    lives_ok ({kvsitr_rewind (NULL);},
        "kvsitr_rewind on NULL iterator doesn't crash");
    lives_ok ({kvsitr_destroy (NULL);},
        "kvsitr_destroy on NULL iterator doesn't crash");

    itr = kvsitr_create (dir);
    ok (itr != NULL,
        "kvsitr_create works");
    ok (kvsitr_next (itr) == NULL,
        "kvsitr_next returns NULL on first call");
    ok (kvsitr_next (itr) == NULL,
        "kvsitr_next returns NULL on second call");
    kvsitr_rewind (itr);
    ok (kvsitr_next (itr) == NULL,
        "kvsitr_next returns NULL after rewind");
    kvsitr_destroy (itr);

    kvsdir_destroy (dir);
}

void test_full (void)
{
    flux_kvsdir_t *dir;
    kvsitr_t *itr;
    json_t *o, *dirent;
    char *s;

    if (!(o = treeobj_create_dir ()))
        BAIL_OUT ("treeobj_create_dir failed");
    if (!(dirent = treeobj_create_symlink ("a.b.c")))
        BAIL_OUT ("treeobj_create_symlink failed");
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

    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed on new treeobj");
    dir = kvsdir_create (NULL, NULL, "foo", s);
    free (s);
    ok (dir != NULL,
        "kvsdir_create works");
    diag ("%s", kvsdir_tostring (dir));

    ok (!kvsdir_exists (dir, "noexist"),
        "kvsdir_exists on nonexistent key returns false");
    ok (kvsdir_exists (dir, "foo"),
        "kvsdir_exists on existing symlink returns true");
    ok (kvsdir_exists (dir, "bar"),
        "kvsdir_exists on existing val returns true");
    ok (kvsdir_exists (dir, "baz"),
        "kvsdir_exists on existing dir returns true");

    ok (!kvsdir_isdir (dir, "noexist"),
        "kvsdir_isdir on nonexistent key returns false");
    ok (!kvsdir_isdir (dir, "foo"),
        "kvsdir_isdir on existing symlink returns false");
    ok (!kvsdir_isdir (dir, "bar"),
        "kvsdir_isdir on existing val returns false");
    ok (kvsdir_isdir (dir, "baz"),
        "kvsdir_isdir on existing symlink returns true");

    ok (!kvsdir_issymlink (dir, "noexist"),
        "kvsdir_issymlink on nonexistent key returns false");
    ok (kvsdir_issymlink (dir, "foo"),
        "kvsdir_issymlink on existing symlink returns true");
    ok (!kvsdir_issymlink (dir, "bar"),
        "kvsdir_issymlink on existing val returns false");
    ok (!kvsdir_issymlink (dir, "baz"),
        "kvsdir_issymlink on existing dir returns false");

    ok (kvsdir_get_size (dir) == 3,
        "kvsdir_get_size returns 3");

    itr = kvsitr_create (dir);
    ok (itr != NULL,
        "kvsitr_create works");
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL on first call");
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL on second call");
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL on third call");
    ok (kvsitr_next (itr) == NULL,
        "kvsitr_next returns NULL on fourth call");

    kvsitr_rewind (itr);
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL after rewind");
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL on second call");
    ok (kvsitr_next (itr) != NULL,
        "kvsitr_next returns non-NULL on third call");
    ok (kvsitr_next (itr) == NULL,
        "kvsitr_next returns NULL on fourth call");
    kvsitr_destroy (itr);

    kvsdir_destroy (dir);
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

