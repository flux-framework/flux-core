#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/log.h"

void kput_txn (flux_kvs_txn_t *txn, const char *s, int val)
{
    char key[128];
    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (flux_kvs_txn_pack (txn, 0, key, "i", val) < 0)
        log_err_exit ("flux_kvs_txn_pack %s=%d", key, val);
    log_msg ("flux_kvs_txn_pack %s=%d", key, val);
}

void kput (flux_t *h, const char *s, int val)
{
    char key[128];
    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (flux_kvs_put_int (h, key, val) < 0)
        log_err_exit ("flux_kvs_put_int %s=%d", key, val);
    log_msg ("flux_kvs_put_int %s=%d", key, val);
}

void kcommit (flux_t *h)
{
    if (flux_kvs_commit_anon (h, 0) < 0)
        log_err_exit ("flux_kvs_commit_anon");
    log_msg ("flux_kvs_commit_anon");
}

void kfence (flux_t *h, const char *s)
{
    char name[128];
    snprintf (name, sizeof (name), "test.asyncfence.%s", s);
    if (flux_kvs_fence_anon (h, name, 1, 0) < 0)
        log_err_exit ("flux_kvs_fence_anon %s", name);
    log_msg ("flux_kvs_fence_anon %s", name);
}

void kget_xfail (flux_t *h, const char *s)
{
    char key[128];
    int val;
    flux_future_t *f;

    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (!(f = flux_kvs_lookup (h, 0, key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get_unpack (f, "i", &val) == 0)
        log_msg_exit ("flux_kvs_lookup_get_unpack(i) %s=%d (expected failure)", key, val);
    log_msg ("flux_kvs_lookup_get_unpack(i) %s failed (expected)", key);
    flux_future_destroy (f);
}

void kget (flux_t *h, const char *s, int expected)
{
    char key[128];
    int val;
    flux_future_t *f;

    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (!(f = flux_kvs_lookup (h, 0, key)))
        log_err_exit ("flux_kvs_lookup");
    if (flux_kvs_lookup_get_unpack (f, "i", &val) < 0)
        log_msg_exit ("flux_kvs_lookup_get_unpack(i) %s", key);
    if (expected != val)
        log_msg_exit ("flux_kvs_lookup_get_unpack(i) %s=%d (expected %d)",
                      key, val, expected);
    log_msg ("flux_kvs_lookup_get_unpack(i) %s=%d", key, val);
    flux_future_destroy (f);
}

void kunlink (flux_t *h, const char *s)
{
    char key[128];
    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (flux_kvs_unlink (h, key) < 0)
        log_err_exit ("kvs_unlink %s", key);
    log_msg ("kvs_unlink %s", key);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;
    flux_kvs_txn_t *txn;

    log_init ("asynfence");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* put a=42
     * fence_begin 1
     * put b=43
     * fence_finish 1
     * get a,b (should be 42,fail)
     * fence 2
     * get a,b (should be 42,43)
     */
    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    kput_txn (txn, "a", 42);
    if (!(f = flux_kvs_fence (h, 0, "test.asyncfence.1", 1, txn)))
        log_err_exit ("flux_kvs_fence 1");
    flux_kvs_txn_destroy (txn);
    log_msg ("BEGIN fence 1");

    kput (h, "b", 43);
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_future_get");
    flux_future_destroy (f);
    log_msg ("FINISH fence 1");
    kget (h, "a", 42);
    kget_xfail (h, "b");
    kfence (h, "2");
    kget (h, "a", 42);
    kget (h, "b", 43);

    /* Clean up
     */
    flux_kvs_unlink (h, "test.asyncfence");
    kcommit (h);

    flux_close (h);
    log_fini ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
