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
#    include "config.h"
#endif
#include <sys/time.h>
#include <sys/resource.h>
#include <assert.h>
#include <stdarg.h>
#include <czmq.h>
#include <stdlib.h>
#if HAVE_LIBJUDY
#    include <Judy.h>
#endif
#if HAVE_SOPHIA
#    include <sophia.h>
#endif
#include <sqlite3.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/log.h"
#if HAVE_LSD_HASH
#    include "src/common/liblsd/hash.h"
#endif
#if HAVE_HATTRIE
#    include "src/common/libhat-trie/hat-trie.h"
#endif

// const int num_keys = 1024*1024;
const int num_keys = 1024 * 1024 * 10;

struct hash_impl {
    void (*destroy) (struct hash_impl *h);
    void (*insert) (struct hash_impl *h, zlist_t *items);
    void (*lookup) (struct hash_impl *h, zlist_t *items);
    void *h;
};

struct item {
    zdigest_t *zd;
    char data[16];
    uint8_t key[20];
    char skey[41];
};

void rusage (struct rusage *res)
{
    int rc = getrusage (RUSAGE_SELF, res);
    assert (rc == 0);
}

long int rusage_maxrss_since (struct rusage *res)
{
    struct rusage new;
    int rc = getrusage (RUSAGE_SELF, &new);
    assert (rc == 0);
    return new.ru_maxrss - res->ru_maxrss;
}

struct item *item_create (int id)
{
    struct item *item = xzmalloc (sizeof (*item));
    assert (item != NULL);
    snprintf (item->data, sizeof (item->data), "%d", id);

    item->zd = zdigest_new ();
    assert (item->zd != NULL);
    zdigest_update (item->zd, (uint8_t *)item->data, sizeof (item->data));

    assert (zdigest_size (item->zd) == sizeof (item->key));
    memcpy (item->key, zdigest_data (item->zd), zdigest_size (item->zd));

    assert (strlen (zdigest_string (item->zd)) == sizeof (item->skey) - 1);
    strcpy (item->skey, zdigest_string (item->zd));
    zdigest_destroy (&item->zd);

    return item;
}

void item_destroy (void *arg)
{
    struct item *item = arg;
    free (item);
}

zlist_t *create_items (void)
{
    zlist_t *items = zlist_new ();
    struct item *item;
    int i, rc;

    assert (items != NULL);
    for (i = 0; i < num_keys; i++) {
        item = item_create (i);
        rc = zlist_append (items, item);
        assert (rc == 0);
    }
    return items;
}

/* zhash
 */

void insert_zhash (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    int rc;

    item = zlist_first (items);
    while (item != NULL) {
        rc = zhash_insert (impl->h, item->skey, item);
        assert (rc == 0);
        item = zlist_next (items);
    }
}

void lookup_zhash (struct hash_impl *impl, zlist_t *items)
{
    struct item *item, *ip;

    item = zlist_first (items);
    while (item != NULL) {
        ip = zhash_lookup (impl->h, item->skey);
        assert (ip != NULL);
        assert (ip == item);
        item = zlist_next (items);
    }
}

void destroy_zhash (struct hash_impl *impl)
{
    zhash_t *zh = impl->h;
    zhash_destroy (&zh);
    free (impl);
}

struct hash_impl *create_zhash (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    impl->h = zhash_new ();
    assert (impl->h != NULL);
    impl->insert = insert_zhash;
    impl->lookup = lookup_zhash;
    impl->destroy = destroy_zhash;
    return impl;
}

/* zhashx
 */
#if HAVE_ZHASHX_NEW
void insert_zhashx (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    int rc;

    item = zlist_first (items);
    while (item != NULL) {
        rc = zhashx_insert (impl->h, item->key, item);
        assert (rc == 0);
        item = zlist_next (items);
    }
}

void lookup_zhashx (struct hash_impl *impl, zlist_t *items)
{
    struct item *item, *ip;

    item = zlist_first (items);
    while (item != NULL) {
        ip = zhashx_lookup (impl->h, item->key);
        assert (ip != NULL);
        assert (ip == item);
        item = zlist_next (items);
    }
}

void destroy_zhashx (struct hash_impl *impl)
{
    zhashx_t *zh = impl->h;
    zhashx_destroy (&zh);
    free (impl);
}

void *duplicator_zhashx (const void *item)
{
    return (void *)item;
}

void destructor_zhashx (void **item)
{
    *item = NULL;
}

int comparator_zhashx (const void *item1, const void *item2)
{
    return memcmp (item1, item2, 20);
}

size_t hasher_zhashx (const void *item)
{
    return *(size_t *)item;
}

struct hash_impl *create_zhashx (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    impl->h = zhashx_new ();
    assert (impl->h != NULL);

    /* Use 20 byte keys that are not copied.
     */
    zhashx_set_key_duplicator (impl->h, duplicator_zhashx);
    zhashx_set_key_destructor (impl->h, destructor_zhashx);
    zhashx_set_key_comparator (impl->h, comparator_zhashx);
    zhashx_set_key_hasher (impl->h, hasher_zhashx);

    impl->insert = insert_zhashx;
    impl->lookup = lookup_zhashx;
    impl->destroy = destroy_zhashx;
    return impl;
}
#endif /* HAVE_ZHASHX_NEW */

/* lsd-hash
 */

#if HAVE_LSD_HASH
void insert_lsd (struct hash_impl *impl, zlist_t *items)
{
    struct item *item, *ip;

    item = zlist_first (items);
    while (item != NULL) {
        ip = hash_insert (impl->h, item->key, item);
        assert (ip != NULL);
        item = zlist_next (items);
    }
}

void lookup_lsd (struct hash_impl *impl, zlist_t *items)
{
    struct item *item, *ip;

    item = zlist_first (items);
    while (item != NULL) {
        ip = hash_find (impl->h, item->key);
        assert (ip != NULL);
        assert (ip == item);
        item = zlist_next (items);
    }
}

void destroy_lsd (struct hash_impl *impl)
{
    hash_destroy (impl->h);
    free (impl);
}

unsigned int hash_lsd (const void *key)
{
    return *(unsigned int *)key; /* 1st 4 bytes of sha1 */
}

int cmp_lsd (const void *key1, const void *key2)
{
    return memcmp (key1, key2, 20);
}

struct hash_impl *create_lsd (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    impl->h = hash_create (1024 * 1024 * 8, hash_lsd, cmp_lsd, NULL);
    assert (impl->h != NULL);

    impl->insert = insert_lsd;
    impl->lookup = lookup_lsd;
    impl->destroy = destroy_lsd;

    return impl;
}
#endif

/* judy
 */

#if HAVE_LIBJUDY
void insert_judy (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    // Pvoid_t array = NULL;
    PWord_t valp;

    item = zlist_first (items);
    while (item != NULL) {
        JHSI (valp, impl->h, item->key, sizeof (item->key));
        assert (valp != PJERR);
        assert (*valp == (uintptr_t)0); /* nonzero indicates dup */
        *valp = (uintptr_t)item;
        item = zlist_next (items);
    }
}

void lookup_judy (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    // Pvoid_t array = NULL;
    PWord_t valp;

    item = zlist_first (items);
    while (item != NULL) {
        JHSG (valp, impl->h, item->key, sizeof (item->key));
        assert (valp != NULL);
        assert (*valp == (uintptr_t)item);
        item = zlist_next (items);
    }
}

void destroy_judy (struct hash_impl *impl)
{
    Word_t bytes;
    JHSFA (bytes, impl->h);
    msg ("judy freed %lu Kbytes of memory", bytes / 1024);
    free (impl);
}

struct hash_impl *create_judy (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));

    impl->insert = insert_judy;
    impl->lookup = lookup_judy;
    impl->destroy = destroy_judy;

    return impl;
}
#endif

/* sophia
 */

#if HAVE_SOPHIA
void log_sophia_error (void *env, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    char *s = xvasprintf (fmt, ap);
    va_end (ap);

    int error_size;
    char *error = NULL;
    if (env)
        error = sp_getstring (env, "sophia.error", &error_size);
    fprintf (stderr, "%s: %s", s, error ? error : "failure");
    if (error)
        free (error);
    free (s);
}

void insert_sophia (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    void *db, *o;
    int rc;

    db = sp_getobject (impl->h, "db.test");
    if (!db)
        log_sophia_error (impl->h, "db.test");
    assert (db != NULL);

    item = zlist_first (items);
    while (item != NULL) {
        // existence check slows down by about 23X! */
        // o = sp_object (db);
        // assert (o != NULL);
        // rc = sp_setstring (o, "key", item->key, sizeof (item->key));
        // assert (rc == 0);
        // void *result = sp_get (db, o); /* destroys 'o' */
        // assert (result == NULL);

        o = sp_object (db);
        assert (o != NULL);
        rc = sp_setstring (o, "key", item->key, sizeof (item->key));
        assert (rc == 0);
        rc = sp_setstring (o, "value", item, sizeof (*item));
        assert (rc == 0);
        rc = sp_set (db, o); /* destroys 'o', copies item  */
        assert (rc == 0);
        item = zlist_next (items);
    }

    sp_destroy (db);
}

void lookup_sophia (struct hash_impl *impl, zlist_t *items)
{
    struct item *item, *ip;
    void *db, *o, *result;
    int rc, item_size;
    int count = 0;

    db = sp_getobject (impl->h, "db.test");
    if (!db)
        log_sophia_error (impl->h, "db.test");
    assert (db != NULL);

    item = zlist_first (items);
    while (item != NULL) {
        o = sp_object (db);
        assert (o != NULL);
        rc = sp_setstring (o, "key", item->key, sizeof (item->key));
        assert (rc == 0);
        result = sp_get (db, o); /* destroys 'o' */
        assert (result != NULL);
        ip = sp_getstring (result, "value", &item_size);
        assert (ip != NULL);
        assert (item_size == sizeof (*item));
        assert (memcmp (ip, item, item_size) == 0);
        sp_destroy (result);
        item = zlist_next (items);
        count++;
        // if (count % 10000 == 0)
        //    msg ("lookup: %d of %d", count, num_keys);
    }

    sp_destroy (db);
}

void destroy_sophia (struct hash_impl *impl)
{
    sp_destroy (impl->h);
    free (impl);
}

struct hash_impl *create_sophia (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    char template[] = "/tmp/hashtest-sophia.XXXXXX";
    char *path;
    int rc;

    path = mkdtemp (template);
    assert (path != NULL);
    cleanup_push_string (cleanup_directory_recursive, path);
    log_msg ("sophia.path: %s", path);
    impl->h = sp_env ();
    assert (impl->h != NULL);
    rc = sp_setstring (impl->h, "sophia.path", path, 0);
    assert (rc == 0);
    // 16m limit increases memory used during insert by about 2X
    // rc = sp_setint (impl->h, "memory.limit", 1024*1024*16);
    // assert (rc == 0);
    rc = sp_setstring (impl->h, "db", "test", 0);
    assert (rc == 0);
    rc = sp_setstring (impl->h, "db.test.index.key", "string", 0);
    assert (rc == 0);
    // N.B. lz4 slows down lookups by about 4X
    // rc = sp_setstring (impl->h, "db.test.compression", "lz4", 0);
    // assert (rc == 0);
    rc = sp_open (impl->h);
    assert (rc == 0);

    impl->insert = insert_sophia;
    impl->lookup = lookup_sophia;
    impl->destroy = destroy_sophia;

    return impl;
}
#endif

#if HAVE_HATTRIE
void insert_hat (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    value_t *val;

    item = zlist_first (items);
    while (item != NULL) {
        val = hattrie_get (impl->h, (char *)item->key, sizeof (item->key));
        assert (val != NULL);
        assert (*val == 0);
        *(void **)val = item;
        item = zlist_next (items);
    }
}

void lookup_hat (struct hash_impl *impl, zlist_t *items)
{
    struct item *item;
    value_t *val;

    item = zlist_first (items);
    while (item != NULL) {
        val = hattrie_tryget (impl->h, (char *)item->key, sizeof (item->key));
        assert (*(void **)val == item);
        item = zlist_next (items);
    }
}

void destroy_hat (struct hash_impl *impl)
{
    hattrie_free (impl->h);
    free (impl);
}

struct hash_impl *create_hat (void)
{
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    impl->h = hattrie_create ();
    assert (impl->h != NULL);
    impl->insert = insert_hat;
    impl->lookup = lookup_hat;
    impl->destroy = destroy_hat;
    return impl;
}
#endif

void insert_sqlite (struct hash_impl *impl, zlist_t *items)
{
    const char *sql = "INSERT INTO objects (hash,object) values (?1, ?2)";
    sqlite3_stmt *stmt;
    int rc;
    struct item *item;

    rc = sqlite3_prepare_v2 (impl->h, sql, -1, &stmt, NULL);
    assert (rc == SQLITE_OK);

    rc = sqlite3_exec (impl->h, "BEGIN", 0, 0, 0);
    assert (rc == SQLITE_OK);

    item = zlist_first (items);
    while (item != NULL) {
        rc = sqlite3_bind_text (stmt,
                                1,
                                (char *)item->key,
                                sizeof (item->key),
                                SQLITE_STATIC);
        assert (rc == SQLITE_OK);
        rc = sqlite3_bind_blob (stmt, 2, item, sizeof (*item), SQLITE_STATIC);
        assert (rc == SQLITE_OK);
        rc = sqlite3_step (stmt);
        assert (rc == SQLITE_DONE);
        rc = sqlite3_reset (stmt);
        assert (rc == SQLITE_OK);

        item = zlist_next (items);
    }
    rc = sqlite3_exec (impl->h, "COMMIT", 0, 0, 0);
    assert (rc == SQLITE_OK);

    sqlite3_finalize (stmt);
}

void lookup_sqlite (struct hash_impl *impl, zlist_t *items)
{
    const char *sql = "SELECT object FROM objects WHERE hash = ?1 LIMIT 1";
    sqlite3_stmt *stmt;
    int rc;
    struct item *item;
    const void *val;

    rc = sqlite3_prepare_v2 (impl->h, sql, -1, &stmt, NULL);
    assert (rc == SQLITE_OK);

    item = zlist_first (items);
    while (item != NULL) {
        rc = sqlite3_bind_text (stmt,
                                1,
                                (char *)item->key,
                                sizeof (item->key),
                                SQLITE_STATIC);
        assert (rc == SQLITE_OK);
        rc = sqlite3_step (stmt);
        assert (rc == SQLITE_ROW);
        assert (sqlite3_column_type (stmt, 0) == SQLITE_BLOB);
        assert (sqlite3_column_bytes (stmt, 0) == sizeof (*item));
        val = sqlite3_column_blob (stmt, 0);
        assert (val != NULL);
        assert (memcmp (val, item, sizeof (*item)) == 0);

        rc = sqlite3_step (stmt);
        assert (rc == SQLITE_DONE);
        rc = sqlite3_reset (stmt);
        assert (rc == SQLITE_OK);

        item = zlist_next (items);
    }

    sqlite3_finalize (stmt);
}

void destroy_sqlite (struct hash_impl *impl)
{
    sqlite3_close (impl->h);
}

struct hash_impl *create_sqlite (void)
{
    int rc;
    sqlite3 *db;
    char template[] = "/tmp/hashtest-sqlite.XXXXXX";
    char *path;
    struct hash_impl *impl = xzmalloc (sizeof (*impl));
    char dbpath[PATH_MAX];

    path = mkdtemp (template);
    assert (path != NULL);
    cleanup_push_string (cleanup_directory_recursive, path);
    log_msg ("sqlite path: %s", path);

    snprintf (dbpath, sizeof (dbpath), "%s/db", path);
    rc = sqlite3_open (dbpath, &db);
    assert (rc == SQLITE_OK);

    // avoid creating journal
    rc = sqlite3_exec (db, "PRAGMA journal_mode=OFF", NULL, NULL, NULL);
    assert (rc == SQLITE_OK);

    // avoid fsync
    rc = sqlite3_exec (db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
    assert (rc == SQLITE_OK);

    // avoid mutex locking
    rc = sqlite3_exec (db, "PRAGMA locking_mode=EXCLUSIVE", NULL, NULL, NULL);
    assert (rc == SQLITE_OK);

    // raise max db pages cached in memory from 2000
    // rc = sqlite3_exec (db, "PRAGMA cache_size=16000", NULL, NULL, NULL);
    // assert (rc == SQLITE_OK);

    // raise db page size from default 1024 bytes
    // N.B. must set before table create
    // rc = sqlite3_exec (db, "PRAGMA page_size=4096", NULL, NULL, NULL);
    // assert (rc == SQLITE_OK);

    rc = sqlite3_exec (db,
                       "CREATE TABLE objects("
                       "hash CHAR(20) PRIMARY KEY,"
                       "object BLOB"
                       ");",
                       NULL,
                       NULL,
                       NULL);
    assert (rc == SQLITE_OK);

    impl->h = db;
    impl->insert = insert_sqlite;
    impl->lookup = lookup_sqlite;
    impl->destroy = destroy_sqlite;
    return impl;
}

void usage (void)
{
    fprintf (stderr,
             "Usage: hashtest zhash | zhashx | judy | lsd | hat"
             " | sophia | sqlite\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    zlist_t *items;
    struct hash_impl *impl = NULL;
    struct timespec t0;
    struct rusage res;

    if (argc == 1)
        usage ();

    rusage (&res);
    monotime (&t0);
    if (!strcmp (argv[1], "zhash"))
        impl = create_zhash ();
#if HAVE_ZHASHX_NEW
    else if (!strcmp (argv[1], "zhashx"))
        impl = create_zhashx ();
#endif
#if HAVE_LSD_HASH
    else if (!strcmp (argv[1], "lsd"))
        impl = create_lsd ();
#endif
#if HAVE_LIBJUDY
    else if (!strcmp (argv[1], "judy"))
        impl = create_judy ();
#endif
#if HAVE_SOPHIA
    else if (!strcmp (argv[1], "sophia"))
        impl = create_sophia ();
#endif
#if HAVE_HATTRIE
    else if (!strcmp (argv[1], "hat"))
        impl = create_hat ();
#endif
    else if (!strcmp (argv[1], "sqlite"))
        impl = create_sqlite ();
    if (!impl)
        usage ();
    log_msg ("create hash: %.2fs (%+ldK)",
             monotime_since (t0) * 1E-3,
             rusage_maxrss_since (&res));

    rusage (&res);
    monotime (&t0);
    items = create_items ();
    log_msg ("create items: %.2fs (%+ldK)",
             monotime_since (t0) * 1E-3,
             rusage_maxrss_since (&res));

    rusage (&res);
    monotime (&t0);
    impl->insert (impl, items);
    log_msg ("insert items: %.2fs (%+ldK)",
             monotime_since (t0) * 1E-3,
             rusage_maxrss_since (&res));

    rusage (&res);
    monotime (&t0);
    impl->lookup (impl, items);
    log_msg ("lookup items: %.2fs (%+ldK)",
             monotime_since (t0) * 1E-3,
             rusage_maxrss_since (&res));

    impl->destroy (impl);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
