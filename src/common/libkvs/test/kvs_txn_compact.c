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


/* Decode a 'val' containing base64 encoded emptiness.
 */
int check_null_value (json_t *dirent)
{
    char *data = "";
    int len = -1;

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

int check_raw_value (json_t *dirent, const char *expected, int expected_len)
{
    char *data = NULL;
    int len;
    int rc = -1;

    if (treeobj_decode_val (dirent, (void **)&data, &len) < 0) {
        diag ("%s: initial base64 decode failed", __FUNCTION__);
        return -1;
    }
    if (len == expected_len
        && memcmp (data, expected, len) == 0)
        rc = 0;
    free (data);
    return rc;
}

int main (int argc, char *argv[])
{
    flux_kvs_txn_t *txn;
    int rc;
    const char *key;
    json_t *entry, *dirent;
    int flags;

    plan (NO_PLAN);

    errno = 0;
    ok (txn_compact (NULL) < 0
        && errno == EINVAL,
        "txn_compact fails on bad input");

    /* Test 0: append consolidate corner cases
     */
    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success on 0 length txns");

    flux_kvs_txn_destroy (txn);

    /* Test 1: basic consolidation works */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "C");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 1,
        "txn contains 1 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "ABC", 3) == 0,
        "1: consolidated foo = ABC");

    flux_kvs_txn_destroy (txn);

    /* Test 2: other keys aren't affected */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "bar", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "C");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 2,
        "txn contains 2 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "AC", 2) == 0,
        "1: consolidated foo = AC");
    ok (txn_get_op (txn, 1, &entry) == 0
        && entry != NULL,
        "2: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "2: txn_decode_op works");
    ok (streq (key, "bar")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "B", 1) == 0,
        "2: bar = B");

    flux_kvs_txn_destroy (txn);

    /* Test 3: can consolidate on multiple keys */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "bar", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "C");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "bar", "D");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 4,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 2,
        "txn contains 2 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "AC", 2) == 0,
        "1: consolidated foo = AC");
    ok (txn_get_op (txn, 1, &entry) == 0
        && entry != NULL,
        "2: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "2: txn_decode_op works");
    ok (streq (key, "bar")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "BD", 2) == 0,
        "2: bar = BD");

    flux_kvs_txn_destroy (txn);

    /* Test 4: non-append before appends ok */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, 0, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "C");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 2,
        "txn contains 2 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == 0
        && check_raw_value (dirent, "A", 1) == 0,
        "1: consolidated foo = A");
    ok (txn_get_op (txn, 1, &entry) == 0
        && entry != NULL,
        "2: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "2: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "BC", 2) == 0,
        "2: foo = BC");

    flux_kvs_txn_destroy (txn);

    /* Test 5: non-append after append leads to error */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");
    rc = flux_kvs_txn_put (txn, 0, "foo", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");

    ok (txn_compact (txn) < 0
        && errno == EINVAL,
        "txn_compact errors on non-append after append on key \"foo\"");

    flux_kvs_txn_destroy (txn);

    /* Test 6: zero length append consolidation works */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put_raw (txn, FLUX_KVS_APPEND, "foo", NULL, 0);
    ok (rc == 0,
        "flux_kvs_txn_put_raw flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put_raw (txn, FLUX_KVS_APPEND, "foo", NULL, 0);
    ok (rc == 0,
        "flux_kvs_txn_put_raw flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 1,
        "txn contains 1 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_raw_value (dirent, "A", 1) == 0,
        "1: consolidated foo = A");

    flux_kvs_txn_destroy (txn);

    /* Test 7: all zero length append consolidation works */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put_raw (txn, FLUX_KVS_APPEND, "foo", NULL, 0);
    ok (rc == 0,
        "flux_kvs_txn_put_raw flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put_raw (txn, FLUX_KVS_APPEND, "foo", NULL, 0);
    ok (rc == 0,
        "flux_kvs_txn_put_raw flags=FLUX_KVS_APPEND works");
    rc = flux_kvs_txn_put_raw (txn, FLUX_KVS_APPEND, "foo", NULL, 0);
    ok (rc == 0,
        "flux_kvs_txn_put_raw flags=FLUX_KVS_APPEND works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 1,
        "txn contains 1 ops");
    ok (txn_get_op (txn, 0, &entry) == 0
        && entry != NULL,
        "1: retrieved");
    ok (txn_decode_op (entry, &key, &flags, &dirent) == 0,
        "1: txn_decode_op works");
    ok (streq (key, "foo")
        && flags == FLUX_KVS_APPEND
        && check_null_value (dirent) == 0,
        "1: consolidated foo = A");

    flux_kvs_txn_destroy (txn);

    /* Test 8: single append is a no-op */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, FLUX_KVS_APPEND, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 1,
        "txn contains 1 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 1,
        "txn contains 1 ops");

    flux_kvs_txn_destroy (txn);

    /* Test 9: no appending is a no-op */

    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");

    rc = flux_kvs_txn_put (txn, 0, "foo", "A");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");
    rc = flux_kvs_txn_put (txn, 0, "foo", "B");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");
    rc = flux_kvs_txn_put (txn, 0, "foo", "C");
    ok (rc == 0,
        "flux_kvs_txn_put flags=0 works");

    /* Verify transaction contents
     */
    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    ok (txn_compact (txn) == 0,
        "txn_compact returns success");

    ok (txn_get_op_count (txn) == 3,
        "txn contains 3 ops");

    flux_kvs_txn_destroy (txn);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

