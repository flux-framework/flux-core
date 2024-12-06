/************************************************************   \
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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "kvs_txn_private.h"
#include "treeobj.h"


void jdiag (json_t *o)
{
    char *tmp = json_dumps (o, JSON_COMPACT);
    diag ("%s", tmp);
    free (tmp);
}


/* Decode a 'val' containing base64 encoded emptiness.
 */
int check_null_value (json_t *dirent)
{
    char *data = "";
    size_t len = 42;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (len != 0 || data != NULL) {
        diag ("%s: len=%d data=%p", __FUNCTION__, len, data);
        return -1;
    }
    return 0;
}

/* Decode a 'val' containing base64 encoded JSON.
 * Extract a number, and compare it to 'expected'.
 */
int check_int_value (json_t *dirent, int expected)
{
    char *data;
    int rc, i;
    size_t len;
    json_t *val;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (!(val = json_loadb (data, len, JSON_DECODE_ANY, NULL))) {
        diag ("%s: couldn't decode JSON", __FUNCTION__);
        free (data);
        return -1;
    }
    rc = json_unpack (val, "i", &i);
    free (data);
    if (rc < 0) {
        diag ("%s: couldn't find requested JSON value", __FUNCTION__);
        json_decref (val);
        return -1;
    }
    json_decref (val);
    if (i != expected) {
        diag ("%s: expected %d received %d", __FUNCTION__, expected, i);
        return -1;
    }
    return 0;
}

/* Decode a 'val' containing base64 encoded JSON.
 * Extract a string, and compare it to 'expected'.
 */
int check_string_value (json_t *dirent, const char *expected)
{
    char *data;
    int rc;
    size_t len;
    const char *s;
    json_t *val;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (!(val = json_loadb (data, len, JSON_DECODE_ANY, NULL))) {
        diag ("%s: couldn't decode JSON", __FUNCTION__);
        free (data);
        return -1;
    }
    rc = json_unpack (val, "s", &s);
    free (data);
    if (rc < 0) {
        diag ("%s: couldn't find requested JSON value", __FUNCTION__);
        json_decref (val);
        return -1;
    }
    if (!streq (expected, s)) {
        diag ("%s: expected %s received %s", __FUNCTION__, expected, s);
        json_decref (val);
        return -1;
    }
    json_decref (val);
    return 0;
}

int check_raw_value (json_t *dirent, const char *expected, int expected_len)
{
    char *data;
    size_t len;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (len == expected_len
        && memcmp (data, expected, len) == 0)
        return 0;
    return -1;
}

void basic (void)
{
    flux_kvs_txn_t *txn;
    int rc;
    const char *key;
    json_t *entry, *dirent;
    int flags;

    /* Create a transaction
     */
    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");
    ok (flux_kvs_txn_is_empty (txn) == true,
        "flux_kvs_txn_is_empty returns true immediately after create");
    rc = flux_kvs_txn_pack (txn, FLUX_KVS_APPEND, "foo.bar.baz",  "i", 42);
    ok (rc == 0,
        "1: flux_kvs_txn_pack(i) flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_pack (txn, 0, "foo.bar.bleep",  "s", "foo");
    ok (rc == 0,
        "2: flux_kvs_txn_pack(s) works");
    rc = flux_kvs_txn_unlink (txn, 0, "a");
    ok (rc == 0,
        "3: flux_kvs_txn_unlink works");
    rc = flux_kvs_txn_mkdir (txn, 0, "b.b.b");
    ok (rc == 0,
        "4: flux_kvs_txn_mkdir works");
    rc = flux_kvs_txn_symlink (txn, 0, "c.c.c", NULL, "b.b.b");
    ok (rc == 0,
        "5: flux_kvs_txn_symlink works (no namespace)");
    rc = flux_kvs_txn_put (txn, 0, "d.d.d", "43");
    ok (rc == 0,
        "6: flux_kvs_txn_put(i) works");
    rc = flux_kvs_txn_unlink (txn, 0, "e");
    ok (rc == 0,
        "7: flux_kvs_txn_unlink works");
    rc = flux_kvs_txn_put (txn, 0, "nerrrrb", NULL);
    ok (rc == 0,
        "8: flux_kvs_txn_put(NULL) works");
    rc = flux_kvs_txn_symlink (txn, 0, "f.f.f", "g.g.g", "h.h.h");
    ok (rc == 0,
        "9: flux_kvs_txn_symlink works (namespace)");

    /* Verify transaction contents
     */
    ok (flux_kvs_txn_is_empty (txn) == false,
        "flux_kvs_txn_is_empty returns false after transactions added");
    ok (txn_get_op_count (txn) == 9,
        "txn contains 9 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo.bar.baz")
        && flags == FLUX_KVS_APPEND
        && check_int_value (dirent, 42) == 0,
        "1: put foo.bar.baz = 42");

    ok (txn_get_op (txn, 1, &entry) == 0,
        "2: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "2: txn_decode_op works");
    ok (streq (key, "foo.bar.bleep")
        && flags == 0
        && check_string_value (dirent, "foo") == 0,
        "2: put foo.bar.baz = \"foo\"");

    ok (txn_get_op (txn, 2, &entry) == 0 && entry != NULL,
        "3: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "3: txn_decode_op works");
    ok (streq (key, "a")
        && flags == 0
        && json_is_null (dirent),
        "3: unlink a");

    ok (txn_get_op (txn, 3, &entry) == 0 && entry != NULL,
        "4: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "4: txn_decode_op works");
    ok (streq (key, "b.b.b")
        && flags == 0
        && treeobj_is_dir (dirent) && treeobj_get_count (dirent) == 0,
        "4: mkdir b.b.b");

    ok (txn_get_op (txn, 4, &entry) == 0 && entry != NULL,
        "5: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "5: txn_decode_op works");
    ok (streq (key, "c.c.c")
        && flags == 0
        && treeobj_is_symlink (dirent)
        && !json_object_get (treeobj_get_data (dirent),
                             "namespace")
        && streq (json_string_value (json_object_get (treeobj_get_data (dirent),
                                                        "target")),
                    "b.b.b"),
        "5: symlink c.c.c b.b.b (no namespace)");

    ok (txn_get_op (txn, 5, &entry) == 0 && entry != NULL,
        "6: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "6: txn_decode_op works");
    ok (streq (key, "d.d.d")
        && flags == 0
        && check_int_value (dirent, 43) == 0,
        "6: put foo.bar.baz = 43");

    ok (txn_get_op (txn, 6, &entry) == 0 && entry != NULL,
        "7: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "7: txn_decode_op works");
    ok (streq (key, "e")
        && flags == 0
        && json_is_null (dirent),
        "7: unlink e");

    ok (txn_get_op (txn, 7, &entry) == 0 && entry != NULL,
        "8: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "8: txn_decode_op works");
    ok (streq (key, "nerrrrb")
        && flags == 0
        && check_null_value (dirent) == 0,
        "8: put nerrrrb = NULL");

    ok (txn_get_op (txn, 8, &entry) == 0 && entry != NULL,
        "9: retrieved");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "9: txn_decode_op works");
    ok (streq (key, "f.f.f")
        && flags == 0
        && streq (json_string_value (json_object_get (treeobj_get_data (dirent),
                                                      "namespace")),
                  "g.g.g")
        && streq (json_string_value (json_object_get (treeobj_get_data (dirent),
                                                      "target")),
                  "h.h.h"),
        "9: symlink f.f.f g.g.g h.h.h (namespace)");

    errno = 0;
    ok (txn_get_op (txn, 9, &entry) < 0 && errno == EINVAL,
        "10: invalid (end of transaction)");

    ok (flux_kvs_txn_clear (txn) == 0,
        "flux_kvs_txn_clear success");

    ok (txn_get_op_count (txn) == 0,
        "txn contains 0 ops");

    ok (flux_kvs_txn_is_empty (txn) == true,
        "flux_kvs_txn_is_empty returns true after clear");

    flux_kvs_txn_destroy (txn);
}

void test_raw_values (void)
{
    flux_kvs_txn_t *txn;
    char buf[13], *nbuf;
    json_t *entry, *dirent;
    const char *key;
    int flags;
    size_t nlen;

    memset (buf, 'c', sizeof (buf));

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    /* Put an empty buffer.
     */
    ok (flux_kvs_txn_put_raw (txn, 0, "a.a.a", NULL, 0) == 0,
        "flux_kvs_txn_put_raw works on empty buffer");
    /* Put some data
     */
    ok (flux_kvs_txn_put_raw (txn, 0, "a.b.c", buf, sizeof (buf)) == 0,
        "flux_kvs_txn_put_raw works with data");
    /* Get first.
     */
    ok (txn_get_op_count (txn) == 2,
        "txn contains two ops");
    ok (txn_get_op (txn, 0, &entry) == 0 && entry != NULL,
        "retrieved 1st op from txn");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "txn_decode_op works");
    nbuf = buf;
    nlen = sizeof (buf);
    ok (treeobj_decode_val (dirent, (void **)&nbuf, &nlen) == 0,
        "retrieved buffer from dirent");
    ok (nlen == 0,
        "and it is size of zero");
    ok (nbuf == NULL,
        "and buffer is NULL");

    /* Get 2nd
     */
    ok (txn_get_op (txn, 1, &entry) == 0 && entry != NULL,
        "retrieved 2nd op from txn");
    jdiag (entry);
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "txn_decode_op works");
    ok (treeobj_decode_val (dirent, (void **)&nbuf, &nlen) == 0,
        "retrieved buffer from dirent");
    ok (nlen == sizeof (buf),
        "and it is the correct size");
    ok (memcmp (nbuf, buf, nlen) == 0,
        "and it is the correct content");
    free (nbuf);
    flux_kvs_txn_destroy (txn);
}

void test_corner_cases (void)
{
    flux_kvs_txn_t *txn;
    json_t *val;
    json_t *op;
    char *treeobj;
    int rc;

    ok (txn_encode_op (NULL, 0, NULL, NULL) < 0 && errno == EINVAL,
        "txn_encode_op fails on bad input");

    val = treeobj_create_val ("abcd", 4);

    ok (txn_encode_op ("key", 0x44, val, &op) < 0 && errno == EINVAL,
        "txn_encode_op fails on bad flags");

    if (!(txn = flux_kvs_txn_create()))
        BAIL_OUT ("flux_kvs_txn_create failed");

    errno = 0;
    rc = flux_kvs_txn_put (NULL, 0, NULL, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_vpack (NULL, 0, NULL, NULL, (va_list){0});
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_vpack fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_pack (NULL, 0, NULL, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_pack fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_put_raw (NULL, 0, NULL, NULL, 0);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put_raw fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_put_treeobj (NULL, 0, NULL, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put_treeobj fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_mkdir (NULL, 0, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_mkdir fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_unlink (NULL, 0, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_unlink fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_symlink (NULL, 0, NULL, NULL, NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_symlink fails w/ EINVAL on bad inputs");

    errno = 0;
    rc = flux_kvs_txn_clear (NULL);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_clear fails w/ EINVAL on bad input");

    ok (flux_kvs_txn_is_empty (NULL) == true,
        "flux_kvs_txn_is_empty returns true on NULL input");

    errno = 0;
    rc = flux_kvs_txn_put (txn, 0xFFFF, "a", "42");
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put fails with EINVAL on bad flags");

    errno = 0;
    rc = flux_kvs_txn_pack (txn, 0xFFFF, "b",  "s", "foo");
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_pack fails with EINVAL on bad flags");

    errno = 0;
    rc = flux_kvs_txn_put_raw (txn, 0xFFFF, "c", "bar", 3);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put_raw fails with EINVAL on bad flags");

    treeobj = json_dumps (val, 0);
    if (!treeobj)
        BAIL_OUT ("failed to created treeobj string");

    errno = 0;
    rc = flux_kvs_txn_put_treeobj (txn, 0xFFFF, "d", treeobj);
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_put_treeobj fails with EINVAL on bad flags");

    errno = 0;
    rc = flux_kvs_txn_mkdir (txn, 0xFFFF, "e");
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_mkdir fails with EINVAL on bad flags");

    errno = 0;
    rc = flux_kvs_txn_unlink (txn, 0xFFFF, "f");
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_unlink fails with EINVAL on bad flags");

    errno = 0;
    rc = flux_kvs_txn_symlink (txn, 0xFFFF, "g", "ns", "h");
    ok (rc < 0 && errno == EINVAL,
        "flux_kvs_txn_symlink fails with EINVAL on bad flags");

    json_decref (val);
    free (treeobj);
    flux_kvs_txn_destroy (txn);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    basic ();
    test_raw_values ();
    test_corner_cases ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

