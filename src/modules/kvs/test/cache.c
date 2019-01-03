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
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libkvs/treeobj.h"
#include "src/common/libutil/tstat.h"
#include "src/common/libtap/tap.h"
#include "src/modules/kvs/waitqueue.h"
#include "src/modules/kvs/cache.h"

static int cache_entry_set_treeobj (struct cache_entry *entry, const json_t *o)
{
    char *s = NULL;
    int saved_errno;
    int rc = -1;

    if (!entry || !o || treeobj_validate (o) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!(s = treeobj_encode (o)))
        goto done;
    if (cache_entry_set_raw (entry, s, strlen (s)) < 0)
        goto done;
    rc = 0;
done:
    saved_errno = errno;
    free (s);
    errno = saved_errno;
    return rc;
}

void wait_cb (void *arg)
{
    int *count = arg;
    (*count)++;
}

struct wait_error
{
    int count;
    int errnum;
};

void wait_error_cb (void *arg)
{
    struct wait_error *we = arg;
    ok (we->errnum == ENOTSUP,
        "wait error called correctly before callback");
    we->count++;
}

void error_cb (wait_t *wf, int errnum, void *arg)
{
    int *err = arg;
    (*err) = errnum;
}

void cache_tests (void)
{
    struct cache *cache;
    tstat_t ts;
    int size, incomplete, dirty;

    cache_destroy (NULL);
    diag ("cache_destroy accept NULL arg");

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (cache_count_entries (cache) == 0,
        "cache contains 0 entries");
    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 0, "empty cache, ts.n == 0");
    ok (size == 0, "empty cache, size == 0");
    ok (incomplete == 0, "empty cache, incomplete == 0");
    ok (dirty == 0, "empty cache, dirty == 0");
    cache_destroy (cache);
}

void cache_entry_basic_tests (void)
{
    struct cache_entry *e;
    json_t *o;
    char *data;

    /* corner case tests */

    ok (cache_entry_create (NULL) == NULL
        && errno == EINVAL,
        "cache_entry_create fails with EINVAL on bad input");

    cache_entry_destroy (NULL);
    diag ("cache_entry_destroy accept NULL arg");

    ok (cache_entry_set_treeobj (NULL, NULL) < 0
        && errno == EINVAL,
        "cache_entry_set_treeobj fails with EINVAL with bad input");

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create success");

    o = json_string ("yabadabadoo");

    ok (cache_entry_set_treeobj (e, o) < 0
        && errno == EINVAL,
        "cache_entry_set_treeobj fails with EINVAL with non-treeobj json");

    json_decref (o);

    data = strdup ("abcd");

    ok (cache_entry_set_raw (e, data, -1) < 0
        && errno == EINVAL,
        "cache_entry_set_raw fails with EINVAL with bad input");
    ok (cache_entry_set_raw (e, NULL, 5) < 0
        && errno == EINVAL,
        "cache_entry_set_raw fails with EINVAL with bad input");

    free (data);

    ok (cache_entry_set_errnum_on_valid (NULL, EPERM) < 0
        && errno == EINVAL,
        "cache_entry_set_errnum_on_valid returns EINVAL with bad input");

    ok (cache_entry_set_errnum_on_valid (e, 0) < 0
        && errno == EINVAL,
        "cache_entry_set_errnum_on_valid returns EINVAL with bad errnum");

    ok (cache_entry_set_errnum_on_notdirty (NULL, EPERM) < 0
        && errno == EINVAL,
        "cache_entry_set_errnum_on_notdirty returns EINVAL with bad input");

    ok (cache_entry_set_errnum_on_notdirty (e, 0) < 0
        && errno == EINVAL,
        "cache_entry_set_errnum_on_notdirty returns EINVAL with bad errnum");

    cache_entry_destroy (e);
    e = NULL;
}

void cache_entry_raw_tests (void)
{
    struct cache_entry *e;
    json_t *o1;
    char *data, *data2;
    const char *datatmp;
    int len;

    /* test empty cache entry later filled with raw data.
     */

    data = strdup ("abcd");
    data2 = strdup ("abcd");

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_get_valid (e) == false,
        "cache entry initially non-valid");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry initially not dirty");
    ok (cache_entry_set_dirty (e, true) < 0,
        "cache_entry_set_dirty fails b/c entry non-valid");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry does not set dirty, b/c no data");
    ok (cache_entry_get_raw (e, NULL, NULL) < 0,
        "cache_entry_get_raw fails, no data set");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry now valid after cache_entry_set_raw call");

    ok (cache_entry_set_raw (e, data2, strlen (data) + 1) == 0,
        "cache_entry_set_raw again, silent success");
    ok (cache_entry_set_raw (e, NULL, 0) < 0 && errno == EBADE,
        "cache_entry_set_raw fails with EBADE, changing validity type");

    ok (cache_entry_get_raw (e, (const void **)&datatmp, &len) == 0,
        "raw data retrieved from cache entry");
    ok (datatmp && strcmp (datatmp, data) == 0,
        "raw data matches expected string");
    ok (datatmp && (len == strlen (data) + 1),
        "raw data length matches expected length");

    ok (cache_entry_set_dirty (e, true) == 0,
        "cache_entry_set_dirty success");
    ok (cache_entry_get_dirty (e) == true,
        "cache entry succcessfully set dirty");

    ok (cache_entry_clear_dirty (e) == 0,
        "cache_entry_clear_dirty success");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry succcessfully now not dirty, b/c no waiters");

    ok (cache_entry_set_dirty (e, true) == 0,
        "cache_entry_set_dirty success");
    ok (cache_entry_get_dirty (e) == true,
        "cache entry succcessfully set dirty");
    ok (cache_entry_force_clear_dirty (e) == 0,
        "cache_entry_force_clear_dirty success");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry succcessfully now not dirty");

    cache_entry_destroy (e); /* destroys data */
    free (data);
    free (data2);
    e = NULL;

    /* test empty cache entry later filled with zero-byte raw data.
     */

    data = strdup ("abcd");

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, NULL, 0) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry now valid after cache_entry_set_raw call");
    ok (cache_entry_set_raw (e, NULL, 0) == 0,
        "cache_entry_set_raw again, silent success");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) < 0
        && errno == EBADE,
        "cache_entry_set_raw fails with EBADE, changing validity type");

    o1 = treeobj_create_val ("foo", 3);
    ok (cache_entry_set_treeobj (e, o1) < 0
        && errno == EBADE,
        "cache_entry_set_treeobj fails with EBADE, changing validity type");
    json_decref (o1);
    o1 = NULL;

    ok (cache_entry_get_raw (e, (const void **)&datatmp, &len) == 0,
        "raw data retrieved from cache entry");
    ok (datatmp == NULL,
        "raw data is NULL");
    ok (len == 0,
        "raw data length is zero");

    cache_entry_destroy (e);
    free (data);
}

void cache_entry_raw_and_treeobj_tests (void)
{
    struct cache_entry *e;
    json_t *o1, *otest;
    const json_t *otmp;
    char *data;
    const char *datatmp;
    int len;

    /* test cache entry filled with raw data that is not valid treeobj
     */

    data = strdup ("foo");

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_treeobj (e) == NULL,
        "cache_entry_get_treeobj returns NULL for non-treeobj raw data");
    cache_entry_destroy (e);
    free (data);

    /* test cache entry filled with zero length raw data */

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, NULL, 0) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_treeobj (e) == NULL,
        "cache_entry_get_treeobj returns NULL for zero length raw data");
    cache_entry_destroy (e);

    /* test cache entry filled with raw data that happens to be valid
     * treeobj
     */

    o1 = treeobj_create_val ("foo", 3);
    data = treeobj_encode (o1);

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, data, strlen (data)) == 0,
        "cache_entry_set_raw success");
    ok ((otmp = cache_entry_get_treeobj (e)) != NULL,
        "cache_entry_get_treeobj returns non-NULL for treeobj-legal raw data");
    otest = treeobj_create_val ("foo", 3);
    /* XXX - json_equal takes const in jansson > 2.10 */
    ok (json_equal ((json_t *)otmp, otest) == true,
        "treeobj returned from cache entry correct");
    json_decref (o1);
    json_decref (otest);
    free (data);
    cache_entry_destroy (e);

    /* test cache entry filled with treeobj and get raw data */

    o1 = treeobj_create_val ("abcd", 3);
    data = treeobj_encode (o1);

    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_treeobj (e, o1) == 0,
        "cache_entry_set_treeobj success");
    ok (cache_entry_get_raw (e, (const void **)&datatmp, &len) == 0,
        "cache_entry_get_raw returns success for get treeobj raw data");
    ok (datatmp && strncmp (datatmp, data, len) == 0,
        "raw data matches expected string version of treeobj");
    ok (datatmp && (len == strlen (data)),
        "raw data length matches expected length of treeobj string");
    json_decref (o1);
    free (data);
    cache_entry_destroy (e);
}

void waiter_tests (void)
{
    struct cache_entry *e;
    char *data;
    wait_t *w;
    struct wait_error we;
    int count;

    data = strdup ("abcd");

    /* Test cache entry waiters.
     * N.B. waiter is destroyed when run.
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create created empty object");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_clear_dirty (e) < 0,
        "cache_entry_clear_dirty returns error, b/c no object set");
    ok (cache_entry_force_clear_dirty (e) < 0,
        "cache_entry_force_clear_dirty returns error, b/c no object set");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry set valid with one waiter");
    ok (count == 1,
        "waiter callback ran");

    we.count = 0;
    we.errnum = 0;
    ok ((w = wait_create (wait_cb, &we)) != NULL,
        "wait_create works");
    ok (wait_set_error_cb (w, error_cb, &we.errnum) == 0,
        "wait_set_error_cb works");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_entry_set_errnum_on_valid (e, ENOTSUP) == 0,
        "cache_entry_set_errnum_on_valid success");
    ok (we.count == 1,
        "waiter callback ran");
    ok (we.errnum == ENOTSUP,
        "error callback ran");

    we.count = 0;
    we.errnum = 0;
    ok ((w = wait_create (wait_cb, &we)) != NULL,
        "wait_create works");
    ok (wait_set_error_cb (w, error_cb, &we.errnum) == 0,
        "wait_set_error_cb works");
    ok (cache_entry_wait_notdirty (e, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_entry_set_errnum_on_notdirty (e, EPERM) == 0,
        "cache_entry_set_errnum_on_notdirty success");
    ok (we.count == 1,
        "waiter callback ran");
    ok (we.errnum == EPERM,
        "error callback ran");

    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok (cache_entry_set_dirty (e, true) == 0,
        "cache_entry_set_dirty success");
    ok (cache_entry_get_dirty (e) == true,
        "cache entry set dirty, adding waiter");
    ok (cache_entry_wait_notdirty (e, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_entry_clear_dirty (e) == 0,
        "cache_entry_clear_dirty success");
    ok (cache_entry_get_dirty (e) == true,
        "cache entry still dirty, b/c of a waiter");
    ok (cache_entry_set_dirty (e, false) == 0,
        "cache_entry_set_dirty success");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry set not dirty with one waiter");
    ok (count == 1,
        "waiter callback ran");

    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok (cache_entry_set_dirty (e, true) == 0,
        "cache_entry_set_dirty success");
    ok (cache_entry_get_dirty (e) == true,
        "cache entry set dirty, adding waiter");
    ok (cache_entry_wait_notdirty (e, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_entry_force_clear_dirty (e) == 0,
        "cache_entry_force_clear_dirty success");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry set not dirty with one waiter");
    ok (count == 0,
        "waiter callback not called on force clear dirty");

    cache_entry_destroy (e); /* destroys data */

    /* set cache entry to zero-data, should also call get valid
     * waiter */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ("a-reference")) != NULL,
        "cache_entry_create created empty object");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_entry_set_raw (e, NULL, 0) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry set valid with one waiter");
    ok (count == 1,
        "waiter callback ran");
    cache_entry_destroy (e); /* destroys data */

    free (data);
}

void cache_blobref_tests (void)
{
    struct cache *cache;
    struct cache_entry *e;
    const char *ref;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok ((e = cache_entry_create ("abcd")) != NULL,
        "cache_entry_create works");
    ok (cache_insert (cache, e) == 0,
        "cache_insert works");
    ok ((ref = cache_entry_get_blobref (e)) != NULL,
        "cache_entry_get_blobref success");
    ok (!strcmp (ref, "abcd"),
        "cache_entry_get_blobref returned correct ref");

    cache_destroy (cache);
}

void cache_remove_entry_tests (void)
{
    struct cache *cache;
    struct cache_entry *e;
    json_t *o;
    wait_t *w;
    int count;

    ok ((cache = cache_create ()) != NULL,
        "cache_create works");

    ok ((e = cache_entry_create ("remove-ref")) != NULL,
        "cache_entry_create works");
    ok (cache_insert (cache, e) == 0,
        "cache_insert works");
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    ok (cache_remove_entry (cache, "blalalala") == 0,
        "cache_remove_entry failed on bad reference");
    ok (cache_remove_entry (cache, "remove-ref") == 1,
        "cache_remove_entry removed cache entry w/o object");
    ok (cache_lookup (cache, "remove-ref", 0) == NULL,
        "cache_lookup verify entry gone");

    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ("remove-ref")) != NULL,
        "cache_entry_create created empty object");
    ok (cache_insert (cache, e) == 0,
        "cache_insert works");
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry failed on valid waiter");
    o = treeobj_create_val ("foobar", 6);
    ok (cache_entry_set_treeobj (e, o) == 0,
        "cache_entry_set_treeobj success");
    json_decref (o);
    ok (cache_entry_get_valid (e) == true,
        "cache entry set valid with one waiter");
    ok (count == 1,
        "waiter callback ran");
    ok (cache_remove_entry (cache, "remove-ref") == 1,
        "cache_remove_entry removed cache entry after valid waiter gone");
    ok (cache_lookup (cache, "remove-ref", 0) == NULL,
        "cache_lookup verify entry gone");

    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ("remove-ref")) != NULL,
        "cache_entry_create works");
    o = treeobj_create_val ("foobar", 6);
    ok (cache_entry_set_treeobj (e, o) == 0,
        "cache_entry_set_treeobj success");
    json_decref (o);
    ok (cache_insert (cache, e) == 0,
        "cache_insert works");
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    ok (cache_entry_set_dirty (e, true) == 0,
        "cache_entry_set_dirty success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry not removed b/c dirty");
    ok (cache_entry_wait_notdirty (e, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry failed on notdirty waiter");
    ok (cache_entry_set_dirty (e, false) == 0,
        "cache_entry_set_dirty success");
    ok (count == 1,
        "waiter callback ran");
    ok (cache_remove_entry (cache, "remove-ref") == 1,
        "cache_remove_entry removed cache entry after notdirty waiter gone");
    ok (cache_lookup (cache, "remove-ref", 0) == NULL,
        "cache_lookup verify entry gone");

    cache_destroy (cache);
}

void cache_expiration_tests (void)
{
    struct cache *cache;
    struct cache_entry *e1, *e2, *e3, *e4;
    tstat_t ts;
    int size, incomplete, dirty;
    json_t *o1;
    json_t *otest;
    const json_t *otmp;

    /* Put entry in cache and test lookup, expire
     */
    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (cache_count_entries (cache) == 0,
        "cache contains 0 entries");

    /* first test w/ entry w/o treeobj object */
    ok ((e1 = cache_entry_create ("xxx1")) != NULL,
        "cache_entry_create works");
    ok (cache_insert (cache, e1) == 0,
        "cache_insert works");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry after insert");
    ok (cache_lookup (cache, "yyy1", 0) == NULL,
        "cache_lookup of wrong hash fails");
    ok ((e2 = cache_lookup (cache, "xxx1", 42)) != NULL,
        "cache_lookup of correct hash works (last use=42)");
    ok (cache_entry_get_treeobj (e2) == NULL,
        "no treeobj object found");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");
    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 0, "cache w/ entry w/o data, ts.n == 0");
    ok (size == 0, "cache w/ entry w/o data, size == 0");
    ok (incomplete == 1, "cache w/ entry w/o data, incomplete == 1");
    ok (dirty == 0, "cache w/ entry w/o data, dirty == 0");
    ok (cache_expire_entries (cache, 43, 1) == 0,
        "cache_expire_entries now=43 thresh=1 expired 0 b/c entry invalid");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");
    ok (cache_expire_entries (cache, 44, 1) == 0,
        "cache_expire_entries now=44 thresh=1 expired 0");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");

    /* second test w/ entry with treeobj object */
    o1 = treeobj_create_val ("foo", 3);
    ok ((e3 = cache_entry_create ("xxx2")) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_treeobj (e3, o1) == 0,
        "cache_entry_set_treeobj success");
    json_decref (o1);
    ok (cache_insert (cache, e3) == 0,
        "cache_insert works");
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries after insert");
    ok (cache_lookup (cache, "yyy2", 0) == NULL,
        "cache_lookup of wrong hash fails");
    ok ((e4 = cache_lookup (cache, "xxx2", 42)) != NULL,
        "cache_lookup of correct hash works (last use=42)");
    ok ((otmp = cache_entry_get_treeobj (e4)) != NULL,
        "cache_entry_get_treeobj found entry");
    otest = treeobj_create_val ("foo", 3);
    /* XXX - json_equal takes const in jansson > 2.10 */
    ok (json_equal ((json_t *)otmp, otest) == 1,
        "expected treeobj object found");
    json_decref (otest);
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries");

    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 1, "cache w/ entry w/ data, ts.n == 1");
    ok (size != 0, "cache w/ entry w/ data, size != 0");
    ok (incomplete == 1, "cache w/ entry w/ data, incomplete == 1");
    ok (dirty == 0, "cache w/ entry w/ data, dirty == 0");

    ok (cache_entry_set_dirty (e4, true)  == 0,
        "cache_entry_set_dirty success");

    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 1, "cache w/ entry w/ dirty data, ts.n == 1");
    ok (size != 0, "cache w/ entry w/ dirty data, size != 0");
    ok (incomplete == 1, "cache w/ entry w/ dirty data, incomplete == 1");
    ok (dirty == 1, "cache w/ entry w/ dirty data, dirty == 1");

    ok (cache_entry_set_dirty (e4, false) == 0,
        "cache_entry_set_dirty success");

    ok (cache_expire_entries (cache, 43, 1) == 0,
        "cache_expire_entries now=43 thresh=1 expired 0");
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries");
    ok (cache_expire_entries (cache, 44, 1) == 1,
        "cache_expire_entries now=44 thresh=1 expired 1");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");

    cache_destroy (cache);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    cache_tests ();
    cache_entry_basic_tests ();
    cache_entry_raw_tests ();
    cache_entry_raw_and_treeobj_tests ();
    waiter_tests ();
    cache_expiration_tests ();
    cache_blobref_tests ();
    cache_remove_entry_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

