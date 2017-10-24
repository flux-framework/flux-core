#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libutil/tstat.h"
#include "src/common/libtap/tap.h"
#include "src/modules/kvs/waitqueue.h"
#include "src/modules/kvs/cache.h"

void wait_cb (void *arg)
{
    int *count = arg;
    (*count)++;
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
    char *data;

    /* corner case tests */
    ok (cache_entry_set_json (NULL, NULL) < 0
        && errno == EINVAL,
        "cache_entry_set_json fails with EINVAL with bad input");
    cache_entry_destroy (NULL);
    diag ("cache_entry_destroy accept NULL arg");

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create success");

    data = strdup ("abcd");

    ok (cache_entry_set_raw (e, data, -1) < 0
        && errno == EINVAL,
        "cache_entry_set_raw fails with EINVAL with bad input");
    ok (cache_entry_set_raw (e, NULL, 5) < 0
        && errno == EINVAL,
        "cache_entry_set_raw fails with EINVAL with bad input");

    free (data);

    cache_entry_destroy (e);
    e = NULL;
}

void cache_entry_raw_tests (void)
{
    struct cache_entry *e;
    json_t *o1;
    char *data, *data2, *datatmp;
    int len;

    /* test empty cache entry later filled with raw data.
     */

    data = strdup ("abcd");
    data2 = strdup ("abcd");

    ok ((e = cache_entry_create ()) != NULL,
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

    /* cache_entry_set_raw will free data2 */
    ok (cache_entry_set_raw (e, data2, strlen (data) + 1) == 0,
        "cache_entry_set_raw again, silent success");
    ok (cache_entry_set_raw (e, NULL, 0) < 0 && errno == EBADE,
        "cache_entry_set_raw fails with EBADE, changing validity type");

    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    /* cache_entry_set_json will json_decref o1 */
    ok (cache_entry_set_json (e, o1) == 0,
        "cache_entry_set_json, silent success");
    o1 = NULL;

    ok (cache_entry_get_raw (e, (void **)&datatmp, &len) == 0,
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
    e = NULL;

    /* test empty cache entry later filled with zero-byte raw data.
     */

    data = strdup ("abcd");

    ok ((e = cache_entry_create ()) != NULL,
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
    free (data);

    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    /* cache_entry_set_json will json_decref o1 */
    ok (cache_entry_set_json (e, o1) < 0
        && errno == EBADE,
        "cache_entry_set_json fails with EBADE, changing validity type");
    json_decref (o1);
    o1 = NULL;

    ok (cache_entry_get_raw (e, (void **)&datatmp, &len) == 0,
        "raw data retrieved from cache entry");
    ok (datatmp == NULL,
        "raw data is NULL");
    ok (len == 0,
        "raw data length is zero");

    cache_entry_destroy (e);   /* destroys data */
    e = NULL;
}

void cache_entry_json_tests (void)
{
    struct cache_entry *e;
    json_t *otmp, *o1, *o2;
    char *data;

    /* Play with one entry.
     * N.B.: json ref is NOT incremented by create or get_json.
     */

    /* test empty cache entry later filled with json.
     */

    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    o2 = json_object ();
    json_object_set_new (o2, "foo", json_integer (42));

    data = strdup ("abcd");

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_get_valid (e) == false,
        "cache entry initially non-valid");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry initially not dirty");
    ok (cache_entry_set_dirty (e, true) < 0,
        "cache_entry_set_dirty fails b/c entry non-valid");
    ok (cache_entry_get_dirty (e) == false,
        "cache entry does not set dirty, b/c no data");
    ok ((otmp = cache_entry_get_json (e)) == NULL,
        "cache_entry_get_json returns NULL, no json set");
    ok (cache_entry_set_json (e, o1) == 0,
        "cache_entry_set_json success");

    /* cache_entry_set_json will json_decref o2 */
    ok (cache_entry_set_json (e, o2) == 0,
        "cache_entry_set_json again, silent success");
    o2 = NULL;

    ok (cache_entry_get_valid (e) == true,
        "cache entry now valid after cache_entry_set_json call");

    /* cache_entry_set_raw will free data */
    ok (cache_entry_set_raw (e, data, 4) == 0,
        "cache_entry_set_raw, silent success");
    data = NULL;

    ok (cache_entry_set_raw (e, NULL, 0) < 0
        && errno == EBADE,
        "cache_entry_set_raw fails with EBADE, changing validity type");

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

    ok ((o2 = cache_entry_get_json (e)) != NULL,
        "json retrieved from cache entry");
    ok ((otmp = json_object_get (o2, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (otmp) == 42,
        "expected json object found");

    cache_entry_destroy (e); /* destroys o1 */
    e = NULL;
}

void cache_entry_raw_and_json_tests (void)
{
    struct cache_entry *e;
    json_t *o1, *otmp;
    char *data, *datatmp;
    int len;

    /* test cache entry filled with raw data that is not valid json
     */

    data = strdup ("foo");

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok ((otmp = cache_entry_get_json (e)) == NULL,
        "cache_entry_get_json returns NULL for non-json raw data");
    cache_entry_destroy (e);

    /* test cache entry filled with zero length raw data */

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, NULL, 0) == 0,
        "cache_entry_set_raw success");
    ok ((otmp = cache_entry_get_json (e)) == NULL,
        "cache_entry_get_json returns NULL for zero length raw data");
    cache_entry_destroy (e);

    /* test cache entry filled with raw data that happens to be valid
     * json
     */

    data = strdup ("\"foo\"");

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok ((otmp = cache_entry_get_json (e)) != NULL,
        "cache_entry_get_json returns non-NULL for json-legal raw data");
    o1 = json_string ("foo");
    ok (json_equal (o1, otmp) == true,
        "json returned from cache entry correct");
    cache_entry_destroy (e);

    /* test cache entry filled with json and get raw data */

    o1 = json_string ("abcd");

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_json (e, o1) == 0,
        "cache_entry_set_json success");
    ok (cache_entry_get_raw (e, (void **)&datatmp, &len) == 0,
        "cache_entry_get_raw returns success for get json raw data");
    ok (datatmp && strcmp (datatmp, "\"abcd\"") == 0,
        "raw data matches expected string version of json");
    ok (datatmp && (len == strlen ("\"abcd\"") + 1),
        "raw data length matches expected length of json string");
    cache_entry_destroy (e);
}

void waiter_raw_tests (void)
{
    struct cache_entry *e;
    char *data;
    wait_t *w;
    int count;

    /* Test cache entry waiters.
     * N.B. waiter is destroyed when run.
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create created empty object");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_clear_dirty (e) < 0,
        "cache_entry_clear_dirty returns error, b/c no object set");
    ok (cache_entry_force_clear_dirty (e) < 0,
        "cache_entry_force_clear_dirty returns error, b/c no object set");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    data = strdup ("abcd");
    ok (cache_entry_set_raw (e, data, strlen (data) + 1) == 0,
        "cache_entry_set_raw success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry set valid with one waiter");
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
    e = NULL;

    /* set cache entry to zero-data, should also call get valid
     * waiter */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ()) != NULL,
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
    e = NULL;
}

void waiter_json_tests (void)
{
    struct cache_entry *e;
    json_t *o;
    wait_t *w;
    int count;

    /* Test cache entry waiters.
     * N.B. waiter is destroyed when run.
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create created empty object");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_clear_dirty (e) < 0,
        "cache_entry_clear_dirty returns error, b/c no object set");
    ok (cache_entry_force_clear_dirty (e) < 0,
        "cache_entry_force_clear_dirty returns error, b/c no object set");
    o = json_object ();
    json_object_set_new (o, "foo", json_integer (42));
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_entry_set_json (e, o) == 0,
        "cache_entry_set_json success");
    ok (cache_entry_get_valid (e) == true,
        "cache entry set valid with one waiter");
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

    cache_entry_destroy (e); /* destroys o */
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

    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    cache_insert (cache, "remove-ref", e);
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
    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create created empty object");
    cache_insert (cache, "remove-ref", e);
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    ok (cache_entry_get_valid (e) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_wait_valid (e, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry failed on valid waiter");
    o = json_string ("foobar");
    ok (cache_entry_set_json (e, o) == 0,
        "cache_entry_set_json success");
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
    o = json_string ("foobar");
    ok ((e = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_json (e, o) == 0,
        "cache_entry_set_json success");
    cache_insert (cache, "remove-ref", e);
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
    json_t *o2;
    json_t *o3;
    json_t *otmp;

    /* Put entry in cache and test lookup, expire
     */
    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (cache_count_entries (cache) == 0,
        "cache contains 0 entries");

    /* first test w/ entry w/o json object */
    ok ((e1 = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    cache_insert (cache, "xxx1", e1);
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry after insert");
    ok (cache_lookup (cache, "yyy1", 0) == NULL,
        "cache_lookup of wrong hash fails");
    ok (cache_lookup_and_get_json (cache, "yyy1", 0) == NULL,
        "cache_lookup_and_get_json of wrong hash fails");
    ok ((e2 = cache_lookup (cache, "xxx1", 42)) != NULL,
        "cache_lookup of correct hash works (last use=42)");
    ok (cache_lookup_and_get_json (cache, "xxx1", 0) == NULL,
        "cache_lookup_and_get_json of correct hash, but non valid entry fails");
    ok (cache_entry_get_json (e2) == NULL,
        "no json object found");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");
    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 0, "cache w/ entry w/o json, ts.n == 0");
    ok (size == 0, "cache w/ entry w/o json, size == 0");
    ok (incomplete == 1, "cache w/ entry w/o json, incomplete == 1");
    ok (dirty == 0, "cache w/ entry w/o json, dirty == 0");
    ok (cache_expire_entries (cache, 43, 1) == 0,
        "cache_expire_entries now=43 thresh=1 expired 0 b/c entry invalid");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");
    ok (cache_expire_entries (cache, 44, 1) == 0,
        "cache_expire_entries now=44 thresh=1 expired 0");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");

    /* second test w/ entry with json object */
    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    ok ((e3 = cache_entry_create ()) != NULL,
        "cache_entry_create works");
    ok (cache_entry_set_json (e3, o1) == 0,
        "cache_entry_set_json success");
    cache_insert (cache, "xxx2", e3);
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries after insert");
    ok (cache_lookup (cache, "yyy2", 0) == NULL,
        "cache_lookup of wrong hash fails");
    ok ((e4 = cache_lookup (cache, "xxx2", 42)) != NULL,
        "cache_lookup of correct hash works (last use=42)");
    ok ((o2 = cache_entry_get_json (e4)) != NULL,
        "cache_entry_get_json found entry");
    ok ((otmp = json_object_get (o2, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (otmp) == 42,
        "expected json object found");
    ok ((o3 = cache_lookup_and_get_json (cache, "xxx2", 0)) != NULL,
        "cache_lookup_and_get_json of correct hash and valid entry works");
    ok ((otmp = json_object_get (o3, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (otmp) == 42,
        "expected json object found");
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries");

    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 1, "cache w/ entry w/ json, ts.n == 1");
    ok (size != 0, "cache w/ entry w/ json, size != 0");
    ok (incomplete == 1, "cache w/ entry w/ json, incomplete == 1");
    ok (dirty == 0, "cache w/ entry w/ json, dirty == 0");

    ok (cache_entry_set_dirty (e4, true)  == 0,
        "cache_entry_set_dirty success");

    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 1, "cache w/ entry w/ dirty json, ts.n == 1");
    ok (size != 0, "cache w/ entry w/ dirty json, size != 0");
    ok (incomplete == 1, "cache w/ entry w/ dirty json, incomplete == 1");
    ok (dirty == 1, "cache w/ entry w/ dirty json, dirty == 1");

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
    cache_entry_json_tests ();
    cache_entry_raw_and_json_tests ();
    waiter_raw_tests ();
    waiter_json_tests ();
    cache_expiration_tests ();
    cache_remove_entry_tests ();

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

