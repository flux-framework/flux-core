#include <czmq.h>
#include <string.h>
#include "src/common/libpmi/single.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/xzmalloc.h"

int main (int argc, char *argv[])
{
    void *pmi;
    struct pmi_operations *ops;
    int rc, spawned, initialized, rank, size, appnum;
    int kvsname_length, kvskey_length, kvsval_length;
    char *kvsname, *kvsval;

    plan (NO_PLAN);

    pmi = pmi_single_create (&ops);
    ok (pmi != NULL,
        "pmi_single_create works");
    spawned = -1;
    rc = ops->init (pmi, &spawned);
    ok (rc == PMI_SUCCESS && spawned == 0,
        "pmi_single_init works, spawned = 0");
    initialized = -1;
    rc = ops->initialized (pmi, &initialized);
    ok (rc == PMI_SUCCESS && initialized != 0,
        "pmi_single_initialized works, initialized true");
    size = -1;
    rc = ops->get_size (pmi, &size);
    ok (rc == PMI_SUCCESS && size == 1,
        "pmi_single_get_size works, size == 1");
    rank = -1;
    rc = ops->get_rank (pmi, &rank);
    ok (rc == PMI_SUCCESS && rank == 0,
        "pmi_single_get_rank works, rank == 0");
    appnum = -2;
    rc = ops->get_appnum (pmi, &appnum);
    ok (rc == PMI_SUCCESS && appnum >= 0,
        "pmi_single_get_appnum works, appnum positive number");
    size = -1;
    rc = ops->get_universe_size (pmi, &size);
    ok (rc == PMI_SUCCESS && size == 1,
        "pmi_single_get_universe_size works, size == 1");

    kvsname_length = -1;
    rc = ops->kvs_get_name_length_max (pmi, &kvsname_length);
    ok (rc == PMI_SUCCESS && kvsname_length > 0,
        "pmi_single_kvs_get_name_length_max works");
    diag ("kvsname_length: %d", kvsname_length);

    kvsname = xzmalloc (kvsname_length);
    rc = ops->kvs_get_my_name (pmi, kvsname, kvsname_length);
    ok (rc == PMI_SUCCESS && strlen (kvsname) > 0,
        "pmi_single_kvs_get_my_name works");
    diag ("kvsname: %s", kvsname);

    kvskey_length = -1;
    rc = ops->kvs_get_key_length_max (pmi, &kvskey_length);
    ok (rc == PMI_SUCCESS && kvskey_length > 0,
        "pmi_single_kvs_get_key_length_max works");
    diag ("kvskey_length: %d", kvskey_length);

    kvsval_length = -1;
    rc = ops->kvs_get_value_length_max (pmi, &kvsval_length);
    ok (rc == PMI_SUCCESS && kvsval_length > 0,
        "pmi_single_kvs_get_value_length_max works");
    diag ("kvsval_length: %d", kvsval_length);

    kvsval = xzmalloc (kvsval_length);
    rc = ops->kvs_get (pmi, kvsname, "noexist", kvsval, kvsval_length);
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_single_kvs_get unknown fails w/PMI_ERR_INVALID_KEY");

    rc = ops->kvs_put (pmi, kvsname, "foo", "bar");
    ok (rc == PMI_SUCCESS,
        "pmi_single_kvs_put works");
    rc = ops->kvs_commit (pmi, kvsname);
    ok (rc == PMI_SUCCESS,
        "pmi_single_kvs_commit works");
    rc = ops->barrier (pmi);
    ok (rc == PMI_SUCCESS,
        "pmi_single_barrier works");
    rc = ops->kvs_get (pmi, kvsname, "foo", kvsval, kvsval_length);
    ok (rc == PMI_SUCCESS && !strcmp (kvsval, "bar"),
        "pmi_single_kvs_get works ");

    rc = ops->kvs_put (pmi, kvsname, "foo", "bar");
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_single_kvs_put on duplicate key fails w/PMI_ERR_INVALID_KEY");

    rc = ops->finalize (pmi);
    ok (rc == PMI_SUCCESS,
        "pmi_single_finalize works");

    free (kvsval);
    free (kvsname);
    ops->destroy (pmi);
    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
