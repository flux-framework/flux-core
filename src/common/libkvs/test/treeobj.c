#include <string.h>
#include <errno.h>

#include "src/common/libkvs/treeobj.h"

#include "src/common/libtap/tap.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/sha1.h"
#include "src/common/libutil/blobref.h"

const int large_dir_entries = 5000;
json_t *create_large_dir (void)
{
    int i;
    char name[256];
    json_t *ent, *dir = treeobj_create_dir ();
    if (!dir)
        return NULL;
    for (i = 0; i < large_dir_entries; i++) {
        snprintf (name, sizeof (name), "entry-%.10d", i);
        if (!(ent = treeobj_create_symlink ("a.b.c.d"))
                        || treeobj_insert_entry (dir, name, ent) < 0) {
            json_decref (dir);
            return NULL;
        }
    }
    return dir;
}

void diag_json (json_t *o)
{
    char *s = json_dumps (o, JSON_INDENT(4));
    diag ("%s", s ? s : "nil");
    free (s);
}

void test_codec (void)
{
    json_t *cpy, *dir = create_large_dir ();
    char *s, *p;

    if (!dir)
        BAIL_OUT ("could not create %d-entry dir", large_dir_entries);
    s = treeobj_encode (dir);
    ok (s != NULL,
        "encoded %d-entry dir", large_dir_entries);
    if (!s)
        BAIL_OUT ("could not encode %d-entry dir", large_dir_entries);
    ok ((cpy = treeobj_decode (s)) != NULL,
        "decoded %d-entry dir", large_dir_entries);
    if (!cpy)
        diag ("%m");
    if (!cpy)
        BAIL_OUT ("could not continue");
    p = treeobj_encode (cpy);
    ok (p != NULL,
        "re-encoded %d-entry dir", large_dir_entries);
    ok (strcmp (p, s) == 0,
        "and they match");
    free (p);

    free (s);
    json_decref (cpy);
    json_decref (dir);
}

const char *blobrefs[] = {
    "sha1-508259c0f7fd50e47716b50ad1f0fc6ed46017f9",
    "sha1-ded5ba42480fe75dcebba1ce068489ff7be2186a",
    "sha1-da39a3ee5e6b4b0d3255bfef95601890afd80709",
};

void test_valref (void)
{
    json_t *valref;
    const char *blobref;
    json_t *val;

    ok ((valref = treeobj_create_valref (NULL)) != NULL,
        "treeobj_create_valref with no blobrefs works");
    errno = 0;
    ok (treeobj_validate (valref) < 0 && errno == EINVAL,
        "treeobj_validate rejects valref with no blobrefs");
    ok (treeobj_is_valref (valref),
        "treeobj_is_valref returns true");
    ok ((val = treeobj_get_data (valref)) != NULL && json_is_array (val),
        "treeobj_get_data returns JSON_ARRAY type");
    errno = 0;
    ok (treeobj_get_blobref (valref, 0) == NULL && errno == EINVAL,
        "treeobj_get_blobref [0] fails with EINVAL");
    ok (treeobj_append_blobref (valref, "foo") < 0 && errno == EINVAL,
        "treeobj_append_blobref returns EINVAL on bad blobref");
    ok (treeobj_append_blobref (valref, blobrefs[0]) == 0,
        "treeobj_append_blobref works");
    ok (treeobj_validate (valref) == 0,
        "treeobj_validate likes valref now");
    ok (treeobj_get_count (valref) == 1,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (valref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] returns expected blobref");
    ok (treeobj_append_blobref (valref, blobrefs[1]) == 0,
        "treeobj_append_blobref works on 2nd blobref");
    ok (treeobj_get_count (valref) == 2,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (valref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] still returns expected blobref");
    blobref = treeobj_get_blobref (valref, 1);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[1]),
        "treeobj_get_blobref [1] returns expected blobref");
    diag_json (valref);
    json_decref (valref);

    ok ((valref = treeobj_create_valref (blobrefs[0])) != NULL,
        "treeobj_create_valref works with blobref arg");
    ok (treeobj_validate (valref) == 0,
        "treeobj_validate likes valref");
    ok (treeobj_get_count (valref) == 1,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (valref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] returns expected blobref");
    diag_json (valref);
    json_decref (valref);

    char buf[1024];
    const char *blobref2;
    int i;
    memset (buf, 'L', sizeof (buf));
    ok ((valref = treeobj_create_valref_buf ("sha1", 256, buf, sizeof (buf))) != NULL,
        "treeobj_create_valref_buf works on 1024 byte blob");
    diag_json (valref);
    ok (treeobj_get_count (valref) == 4,
        "and maxblob 256 split blob into 4 blobrefs");
    blobref = treeobj_get_blobref (valref, 0);
    for (i = 1; i < 4; i++) {
        blobref2 = treeobj_get_blobref (valref, i);
        if (!blobref || !blobref2 || strcmp (blobref, blobref2) != 0)
            break;
    }
    ok (i == 4,
        "and the four blobrefs are identical");
    json_decref (valref);

    ok ((valref = treeobj_create_valref_buf ("sha256", 0, NULL, 0)) != NULL,
        "treeobj_create_valref_buf works on empty buf");
    diag_json (valref);
    ok (treeobj_get_count (valref) == 1,
        "and valref contains one blobref");
    json_decref (valref);
}

void test_val (void)
{
    json_t *val, *val2;
    char buf[32];
    char *outbuf;
    int outlen;

    memset (buf, 'x', sizeof (buf));

    ok ((val = treeobj_create_val (buf, sizeof (buf))) != NULL,
        "treeobj_create_val works");
    diag_json (val);
    ok (treeobj_is_val (val),
        "treeobj_is_value returns true");
    ok (treeobj_get_count (val) == 1,
        "treeobj_get_count returns 1");
    ok (treeobj_decode_val (val, (void **)&outbuf, &outlen) == 0,
        "treeobj_decode_val works");
    ok (outlen == sizeof (buf),
        "and returned size same as input");
    ok (memcmp (buf, outbuf, outlen) == 0,
        "and returned data same as input");
    ok (outbuf[outlen] == '\0',
        "and includes an extra null terminator");
    free (outbuf);
    ok (treeobj_decode_val (val, (void **)&outbuf, NULL) == 0,
        "treeobj_decode_val works w/o len input");
    free (outbuf);
    ok (treeobj_decode_val (val, NULL, &outlen) == 0,
        "treeobj_decode_val works w/o data pointer input");
    ok (outlen == sizeof (buf),
        "and returned size same as input");

    ok ((val2 = treeobj_create_val (NULL, 0)) != NULL,
        "treeobj_create_val NULL, 0 works");
    diag_json (val2);
    ok (treeobj_decode_val (val2, (void **)&outbuf, &outlen) == 0,
        "treeobj_decode_val works");
    ok (outlen == 0,
        "and returned size = 0");
    ok (outbuf == NULL,
        "and returned data = NULL");

    json_decref (val);
    json_decref (val2);
}

void test_dirref (void)
{
    json_t *dirref;
    const char *blobref;
    json_t *val;

    ok ((dirref = treeobj_create_dirref (NULL)) != NULL,
        "treeobj_create_dirref with no blobrefs works");
    errno = 0;
    ok (treeobj_validate (dirref) < 0 && errno == EINVAL,
        "treeobj_validate rejects dirref with no blobrefs");
    ok (treeobj_is_dirref (dirref),
        "treeobj_is_dirref returns true");
    ok ((val = treeobj_get_data (dirref)) != NULL && json_is_array (val),
        "treeobj_get_data returns JSON_ARRAY type");
    errno = 0;
    ok (treeobj_get_blobref (dirref, 0) == NULL && errno == EINVAL,
        "treeobj_get_blobref [0] fails with EINVAL");
    ok (treeobj_append_blobref (dirref, "foo") < 0 && errno == EINVAL,
        "treeobj_append_blobref returns EINVAL on bad blobref");
    ok (treeobj_append_blobref (dirref, blobrefs[0]) == 0,
        "treeobj_append_blobref works");
    ok (treeobj_validate (dirref) == 0,
        "treeobj_validate likes dirref now");
    ok (treeobj_get_count (dirref) == 1,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (dirref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] returns expected blobref");
    ok (treeobj_append_blobref (dirref, blobrefs[1]) == 0,
        "treeobj_append_blobref works on 2nd blobref");
    ok (treeobj_get_count (dirref) == 2,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (dirref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] still returns expected blobref");
    blobref = treeobj_get_blobref (dirref, 1);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[1]),
        "treeobj_get_blobref [1] returns expected blobref");
    diag_json (dirref);
    json_decref (dirref);

    ok ((dirref = treeobj_create_dirref (blobrefs[0])) != NULL,
        "treeobj_create_dirref works with blobref arg");
    ok (treeobj_validate (dirref) == 0,
        "treeobj_validate likes dirref");
    ok (treeobj_get_count (dirref) == 1,
        "treeobj_get_count returns 1");
    blobref = treeobj_get_blobref (dirref, 0);
    ok (blobref != NULL && !strcmp (blobref, blobrefs[0]),
        "treeobj_get_blobref [0] returns expected blobref");

    diag_json (dirref);
    json_decref (dirref);
}

void test_dir (void)
{
    json_t *dir;
    json_t *str = NULL, *val1 = NULL;
    json_t *i = NULL, *val2 = NULL;
    json_t *nil = NULL, *val3 = NULL;
    json_t *val;

    /* create couple test values */
    val1 = treeobj_create_val ("foo", 4);
    val2 = treeobj_create_val ("42", 3);
    val3 = treeobj_create_val (NULL, 0);
    if (!val1 || !val2 || !val3)
        BAIL_OUT ("can't continue without test values");

    ok ((dir = treeobj_create_dir ()) != NULL,
        "treeobj_create_dir works");
    ok (treeobj_validate (dir) == 0,
        "treeobj_validate likes empty dir");
    ok (treeobj_is_dir (dir),
        "treeobj_is_dir returns true");
    ok ((val = treeobj_get_data (dir)) != NULL && json_is_object (val),
        "treeobj_get_data returns JSON_OBJECT type");

    ok (treeobj_get_count (dir) == 0,
        "treeobj_get_count returns 0");
    ok (treeobj_insert_entry (dir, "foo", val1) == 0
            && treeobj_get_count (dir) == 1
            && treeobj_get_entry (dir, "foo") == val1,
        "treeobj_insert_entry works");
    ok (treeobj_insert_entry (dir, "bar", val1) == 0
            && treeobj_get_count (dir) == 2
            && treeobj_get_entry (dir, "bar") == val1,
        "treeobj_insert_entry same value different key works");
    ok (treeobj_insert_entry (dir, "bar", val2) == 0
            && treeobj_get_count (dir) == 2
            && treeobj_get_entry (dir, "foo") == val1
            && treeobj_get_entry (dir, "bar") == val2,
        "treeobj_insert_entry same key replaces entry");
    ok (treeobj_delete_entry (dir, "bar") == 0
            && treeobj_get_count (dir) == 1,
        "treeobj_delete_entry works");
    ok (treeobj_insert_entry (dir, "nil", val3) == 0
            && treeobj_get_count (dir) == 2
            && treeobj_get_entry (dir, "nil") == val3,
        "treeobj_insert_entry accepts json_null value");
    ok (treeobj_validate (dir) == 0,
        "treeobj_validate likes populated dir");

    errno = 0;
    ok (treeobj_get_entry (val1, "foo") == NULL && errno == EINVAL,
        "treeobj_get_entry fails with EINVAL on non-dir treeobj");
    errno = 0;
    ok (treeobj_delete_entry (val1, "foo") < 0 && errno == EINVAL,
        "treeobj_delete_entry fails with EINVAL on non-dir treeobj");
    errno = 0;
    ok (treeobj_insert_entry (val1, "foo", val1) < 0 && errno == EINVAL,
        "treeobj_insert_entry fails with EINVAL on non-dir treeobj");
    errno = 0;
    ok (treeobj_insert_entry (dir, NULL, val1) < 0 && errno == EINVAL,
        "treeobj_insert_entry fails with EINVAL on NULL key");
    errno = 0;
    ok (treeobj_insert_entry (dir, "baz", NULL) < 0 && errno == EINVAL,
        "treeobj_insert_entry fails with EINVAL on NULL value");
    errno = 0;
    ok (treeobj_get_entry (dir, "noexist") == NULL && errno == ENOENT,
        "treeobj_get_entry fails with ENOENT on unknown key");
    errno = 0;
    ok (treeobj_delete_entry (dir, "noexist") < 0 && errno == ENOENT,
        "treeobj_delete_entry fails with ENOENT on unknown key");

    json_decref (str);
    json_decref (val1);

    json_decref (i);
    json_decref (val2);

    json_decref (nil);
    json_decref (val3);

    diag_json (dir);
    json_decref (dir);
}

void test_copy_dir (void)
{
    json_t *dir, *dircpy;
    json_t *val1, *val2, *val3;
    json_t *test;

    /* create couple test values */
    val1 = treeobj_create_val ("a", 1);
    val2 = treeobj_create_val ("b", 1);
    val3 = treeobj_create_val ("c", 1);
    if (!val1 || !val2 || !val3)
        BAIL_OUT ("can't continue without test values");

    /* First, some corner case tests */

    ok (treeobj_copy_dir (val1) == NULL,
        "tree_copy_dir fails on non-dir");

    /* Next, test to show json_copy() is not safe.
     *
     * json_copy(), which makes a shallow copy of a JSON object,
     * cannot be used directly on a RFC 11 dir object, since the
     * directory itself is one level down under the "data" key.
    */

    ok ((dir = treeobj_create_dir ()) != NULL,
        "treeobj_create_dir works");
    ok (treeobj_insert_entry (dir, "a", val1) == 0,
        "treeobj_insert_entry works");
    ok (treeobj_insert_entry (dir, "b", val2) == 0,
        "treeobj_insert_entry works");
    ok (treeobj_get_count (dir) == 2,
        "treeobj_get_count gets correct count");

    ok ((dircpy = json_copy (dir)) != NULL,
        "json_copy works");

    /* change "b" to "c" in original */
    ok (treeobj_insert_entry (dir, "b", val3) == 0,
        "treeobj_insert_entry works");

    ok (json_equal (dir, dircpy) == 1,
        "dir and dircpy are equal, json_copy() not safe");

    json_decref (dir);
    json_decref (dircpy);

    /* Now test to show treeobj_copy_dir() is safe */

    ok ((dir = treeobj_create_dir ()) != NULL,
        "treeobj_create_dir works");
    ok (treeobj_insert_entry (dir, "a", val1) == 0,
        "treeobj_insert_entry works");
    ok (treeobj_insert_entry (dir, "b", val2) == 0,
        "treeobj_insert_entry works");
    ok (treeobj_get_count (dir) == 2,
        "treeobj_get_count gets correct count");

    ok ((dircpy = treeobj_copy_dir (dir)) != NULL,
        "treeobj_copy_dir works");

    /* change "b" to "c" in original */
    ok (treeobj_insert_entry (dir, "b", val3) == 0,
        "treeobj_insert_entry works");

    ok (json_equal (dir, dircpy) == 0,
        "dir and dircpy are not equal, treeobj_copy_dir() safe");

    /* Make sure values are in the right copies */
    ok ((test = treeobj_get_entry (dir, "b")) != NULL,
        "treeobj_get_entry works");
    ok (json_equal (test, val3) == 1,
        "correct value is in original");

    ok ((test = treeobj_get_entry (dircpy, "b")) != NULL,
        "treeobj_get_entry works");
    ok (json_equal (test, val2) == 1,
        "correct value is in copy");

    json_decref (dir);
    json_decref (dircpy);

    json_decref (val1);
    json_decref (val2);
    json_decref (val3);
}

void test_symlink (void)
{
    json_t *o, *data;

    o = treeobj_create_symlink ("a.b.c");
    ok (o != NULL,
        "treeobj_create_symlink works");
    diag_json (o);
    ok (treeobj_is_symlink (o),
        "treeobj_is_symlink returns true");
    ok ((data = treeobj_get_data (o)) != NULL && json_is_string (data),
        "treobj_get_data returned string");
    ok (!strcmp (json_string_value (data), "a.b.c"),
        "and string has right content");
    json_decref (o);
}

void test_corner_cases (void)
{
    json_t *val, *valref, *dir, *symlink;
    json_t *array, *object;
    char *outbuf;
    int outlen;

    val = treeobj_create_val ("a", 1);
    if (!val)
        BAIL_OUT ("can't continue without test value");

    ok (treeobj_append_blobref (val, blobrefs[0]) < 0 && errno == EINVAL,
        "treeobj_append_blobref returns EINVAL on bad treeobj");

    ok (treeobj_get_blobref (val, 0) == NULL && errno == EINVAL,
        "treeobj_get_blobref returns EINVAL on bad treeobj");

    /* Modify val to have bad type */
    json_object_set_new (val, "type", json_string ("foo"));

    ok (treeobj_validate (val) < 0 && errno == EINVAL,
        "treeobj_validate detects invalid type");

    ok (treeobj_get_count (val) < 0 && errno == EINVAL,
        "treeobj_get_count detects invalid type");

    ok (treeobj_decode (treeobj_encode (val)) == NULL && errno == EPROTO,
        "treeobj_decode returns EPROTO on bad treeobj");

    json_decref (val);

    valref = treeobj_create_valref (NULL);
    if (!valref)
        BAIL_OUT ("can't continue without test value");

    ok (treeobj_validate (valref) < 0 && errno == EINVAL,
        "treeobj_validate detects no valid blobref");

    /* Modify valref to have bad blobref */
    array = json_array ();
    json_array_set_new (array, 0, json_string ("sha1-foo"));
    json_object_set_new (valref, "data", array);

    ok (treeobj_validate (valref) < 0 && errno == EINVAL,
        "treeobj_validate detects bad ref in valref");

    json_object_set_new (valref, "data", json_string ("not-array"));

    ok (treeobj_validate (valref) < 0 && errno == EINVAL,
        "treeobj_validate detects bad data in valref");

    json_decref (valref);

    dir = treeobj_create_dir ();
    if (!dir)
        BAIL_OUT ("can't continue without test value");

    ok (treeobj_decode_val (dir, (void **)&outbuf, &outlen) < 0
        && errno == EINVAL,
        "treeobj_decode_val returns EINVAL on non-val treeobj");

    /* Modify valref to have bad blobref */
    object = json_object ();
    json_object_set_new (object, "a", json_string ("foo"));
    json_object_set_new (dir, "data", object);

    ok (treeobj_validate (dir) < 0 && errno == EINVAL,
        "treeobj_validate detects bad treeobj in dir");

    /* Modify dir to have bad data */
    json_object_set_new (dir, "data", json_integer (42));

    ok (treeobj_validate (dir) < 0 && errno == EINVAL,
        "treeobj_validate detects bad data in dir");

    json_decref (dir);

    symlink = treeobj_create_symlink ("some-string");
    if (!symlink)
        BAIL_OUT ("can't continue without test value");

    /* Modify symlink to have bad data */
    json_object_set_new (symlink, "data", json_integer (42));

    ok (treeobj_validate (symlink) < 0 && errno == EINVAL,
        "treeobj_validate detects bad data in symlink");

    json_decref (symlink);
}

int main(int argc, char** argv)
{
    plan (NO_PLAN);

    test_valref ();
    test_val ();
    test_dirref ();
    test_dir ();
    test_copy_dir ();
    test_symlink ();
    test_corner_cases ();

    test_codec ();

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
