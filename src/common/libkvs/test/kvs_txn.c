#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>

#include "kvs_txn.h"
#include "kvs_txn_private.h"
#include "src/common/libtap/tap.h"

void jdiag (json_t *o)
{
    char *tmp = json_dumps (o, JSON_COMPACT);
    diag ("%s", tmp);
    free (tmp);
}

void basic (void)
{
    flux_kvs_txn_t *txn;
    int i, rc;
    const char *key, *s;
    json_t *entry;

    /* create transaction and add two "put" ops
     */
    txn = flux_kvs_txn_create ();
    ok (txn != NULL,
        "flux_kvs_txn_create works");
    rc = flux_kvs_txn_pack (txn, 0, "foo.bar.baz",  "i", 42); // 1
    ok (rc == 0,
        "flux_kvs_txn_pack(i) works");
    rc = flux_kvs_txn_pack (txn, 0, "foo.bar.bleep",  "s", "foo"); // 2
    ok (rc == 0,
        "flux_kvs_txn_pack(s) works");

    /* verify first two ops
     */
    ok (txn_get (txn, TXN_GET_FIRST, &entry) == 0
        && entry != NULL,
        "op-1: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:{s:i}}", "key", &key,
                                              "dirent", "FILEVAL", &i);
    diag ("rc = %d" ,rc);
    ok (rc == 0 && !strcmp (key, "foo.bar.baz") && i == 42,
        "op-1: put foo.bar.baz = 42");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0,
        "op-2: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:{s:s}}", "key", &key,
                                              "dirent", "FILEVAL", &s);
    ok (rc == 0 && !strcmp (key, "foo.bar.bleep") && !strcmp (s, "foo"),
        "op-2: put foo.bar.baz = \"foo\"");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry == NULL,
        "op-3: NULL");

    /* add one of each of the other types of ops
     */
    rc = flux_kvs_txn_unlink (txn, 0, "a"); // 3
    ok (rc == 0,
        "flux_kvs_txn_unlink works");
    rc = flux_kvs_txn_mkdir (txn, 0, "b.b.b"); // 4
    ok (rc == 0,
        "flux_kvs_txn_mkdir works");
    rc = flux_kvs_txn_symlink (txn, 0, "c.c.c", "b.b.b"); // 5
    ok (rc == 0,
        "flux_kvs_txn_symlink works");

    /* verify ops
     */
    ok (txn_get (txn, TXN_GET_FIRST, NULL) == 0,
        "op-1: skip");
    ok (txn_get (txn, TXN_GET_NEXT, NULL) == 0,
        "op-2: skip");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "op-3: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:n}", "key", &key,
                                          "dirent");

    ok (rc == 0 && !strcmp (key, "a"),
        "op-3: unlink a");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "op-4: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:{s:{}}}", "key", &key,
                                               "dirent", "DIRVAL");

    ok (rc == 0 && !strcmp (key, "b.b.b"),
        "op-4: mkdir b.b.b");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry != NULL,
        "op-5: retrieved");
    jdiag (entry);
    rc = json_unpack (entry, "{s:s s:{s:s}}", "key", &key,
                                              "dirent", "LINKVAL", &s);
    ok (rc == 0 && !strcmp (key, "c.c.c") && !strcmp (s, "b.b.b"),
        "op-5: symlink c.c.c b.b.b");
    ok (txn_get (txn, TXN_GET_NEXT, &entry) == 0 && entry == NULL,
        "op-6: NULL");

    flux_kvs_txn_destroy (txn);

}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    basic ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

