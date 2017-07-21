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


int main (int argc, char *argv[])
{
    struct cache *cache;
    struct cache_entry *e1, *e2, *e3, *e4, *e5;
    tstat_t ts;
    int size, incomplete, dirty;
    json_t *o1;
    json_t *o2;
    json_t *o3;
    json_t *o4;
    json_t *o;
    wait_t *w;
    int count;

    plan (NO_PLAN);

    cache_destroy (NULL);
    cache_entry_destroy (NULL);
    diag ("cache_destroy and cache_entry_destroy accept NULL arg");

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

    /* Play with one entry.
     * N.B.: json ref is NOT incremented by create or get_json.
     */
    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    ok ((e1 = cache_entry_create (o1)) != NULL,
        "cache_entry_create works");
    ok (cache_entry_get_valid (e1) == true,
        "cache entry initially valid");
    ok (cache_entry_get_dirty (e1) == false,
        "cache entry initially not dirty");
    cache_entry_set_dirty (e1, true);
    ok (cache_entry_get_dirty (e1) == true,
        "cache entry succcessfully set dirty");
    ok (cache_entry_clear_dirty (e1) == 0,
        "cache_entry_clear_dirty returns 0, b/c no waiters");
    ok (cache_entry_get_dirty (e1) == false,
        "cache entry succcessfully now not dirty");
    ok ((o2 = cache_entry_get_json (e1)) != NULL,
        "json retrieved from cache entry");
    ok ((o = json_object_get (o2, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (o) == 42,
        "expected json object found");
    cache_entry_destroy (e1); /* destroys o1 */

    /* Test cache entry waiters.
     * N.B. waiter is destroyed when run.
     */
    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    ok ((e1 = cache_entry_create (NULL)) != NULL,
        "cache_entry_create created empty object");
    ok (cache_entry_get_valid (e1) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_clear_dirty (e1) < 0,
        "cache_entry_clear_dirty returns error, b/c no object set");
    o1 = json_object ();
    json_object_set_new (o1, "foo", json_integer (42));
    ok (cache_entry_wait_valid (e1, w) == 0,
        "cache_entry_wait_valid success");
    cache_entry_set_json (e1, o1);
    ok (cache_entry_get_valid (e1) == true,
        "cache entry set valid with one waiter");
    ok (count == 1,
        "waiter callback ran");

    count = 0;
    ok ((w = wait_create (wait_cb, &count)) != NULL,
        "wait_create works");
    cache_entry_set_dirty (e1, true);
    ok (cache_entry_get_dirty (e1) == true,
        "cache entry set dirty, adding waiter");
    ok (cache_entry_wait_notdirty (e1, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_entry_clear_dirty (e1) == 1,
        "cache_entry_clear_dirty returns 1, b/c of a waiter");
    cache_entry_set_dirty (e1, false);
    ok (cache_entry_get_dirty (e1) == false,
        "cache entry set not dirty with one waiter");
    ok (count == 1,
        "waiter callback ran");
    cache_entry_destroy (e1); /* destroys o1 */

    /* Put entry in cache and test lookup, expire
     */
    ok ((cache = cache_create ()) != NULL,
        "cache_create works");
    ok (cache_count_entries (cache) == 0,
        "cache contains 0 entries");

    /* first test w/ entry w/o json object */
    ok ((e1 = cache_entry_create (NULL)) != NULL,
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
    ok ((e3 = cache_entry_create (o1)) != NULL,
        "cache_entry_create works");
    cache_insert (cache, "xxx2", e3);
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries after insert");
    ok (cache_lookup (cache, "yyy2", 0) == NULL,
        "cache_lookup of wrong hash fails");
    ok ((e4 = cache_lookup (cache, "xxx2", 42)) != NULL,
        "cache_lookup of correct hash works (last use=42)");
    ok ((o2 = cache_entry_get_json (e4)) != NULL,
        "cache_entry_get_json found entry");
    ok ((o = json_object_get (o2, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (o) == 42,
        "expected json object found");
    ok ((o3 = cache_lookup_and_get_json (cache, "xxx2", 0)) != NULL,
        "cache_lookup_and_get_json of correct hash and valid entry works");
    ok ((o = json_object_get (o3, "foo")) != NULL,
        "json_object_get success");
    ok (json_integer_value (o) == 42,
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

    cache_entry_set_dirty (e4, true);

    memset (&ts, 0, sizeof (ts));
    ok (cache_get_stats (cache, &ts, &size, &incomplete, &dirty) == 0,
        "cache_get_stats works");
    ok (ts.n == 1, "cache w/ entry w/ dirty json, ts.n == 1");
    ok (size != 0, "cache w/ entry w/ dirty json, size != 0");
    ok (incomplete == 1, "cache w/ entry w/ dirty json, incomplete == 1");
    ok (dirty == 1, "cache w/ entry w/ dirty json, dirty == 1");

    cache_entry_set_dirty (e4, false);

    ok (cache_expire_entries (cache, 43, 1) == 0,
        "cache_expire_entries now=43 thresh=1 expired 0");
    ok (cache_count_entries (cache) == 2,
        "cache contains 2 entries");
    ok (cache_expire_entries (cache, 44, 1) == 1,
        "cache_expire_entries now=44 thresh=1 expired 1");
    ok (cache_count_entries (cache) == 1,
        "cache contains 1 entry");

    /* cache_remove_entry tests */

    ok ((e5 = cache_entry_create (NULL)) != NULL,
        "cache_entry_create works");
    cache_insert (cache, "remove-ref", e5);
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
    ok ((e5 = cache_entry_create (NULL)) != NULL,
        "cache_entry_create created empty object");
    cache_insert (cache, "remove-ref", e5);
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    ok (cache_entry_get_valid (e5) == false,
        "cache entry invalid, adding waiter");
    ok (cache_entry_wait_valid (e5, w) == 0,
        "cache_entry_wait_valid success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry failed on valid waiter");
    o4 = json_string ("foobar");
    cache_entry_set_json (e5, o4);
    ok (cache_entry_get_valid (e5) == true,
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
    o4 = json_string ("foobar");
    ok ((e5 = cache_entry_create (o4)) != NULL,
        "cache_entry_create created empty object");
    cache_insert (cache, "remove-ref", e5);
    ok (cache_lookup (cache, "remove-ref", 0) != NULL,
        "cache_lookup verify entry exists");
    cache_entry_set_dirty (e5, true);
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry not removed b/c dirty");
    ok (cache_entry_wait_notdirty (e5, w) == 0,
        "cache_entry_wait_notdirty success");
    ok (cache_remove_entry (cache, "remove-ref") == 0,
        "cache_remove_entry failed on notdirty waiter");
    cache_entry_set_dirty (e5, false);
    ok (count == 1,
        "waiter callback ran");
    ok (cache_remove_entry (cache, "remove-ref") == 1,
        "cache_remove_entry removed cache entry after notdirty waiter gone");
    ok (cache_lookup (cache, "remove-ref", 0) == NULL,
        "cache_lookup verify entry gone");

    cache_destroy (cache);

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

