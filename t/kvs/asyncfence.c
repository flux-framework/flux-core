#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/log.h"

void kput (flux_t *h, const char *s, int val)
{
    char key[128];
    snprintf (key, sizeof (key), "test.asyncfence.%s", s);
    if (kvs_put_int (h, key, val) < 0)
        log_err_exit ("kvs_put_int %s=%d", key, val);
    log_msg ("kvs_put_int %s=%d", key, val);
}

void kcommit (flux_t *h)
{
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
    log_msg ("kvs_commit");
}

void kfence (flux_t *h, const char *s)
{
    char name[128];
    snprintf (name, sizeof (name), "test.asyncfence.%s", s);
    if (kvs_fence (h, name, 1, 0) < 0)
        log_err_exit ("kvs_fence %s", name);
    log_msg ("kvs_fence %s", name);
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
    if (kvs_unlink (h, key) < 0)
        log_err_exit ("kvs_unlink %s", key);
    log_msg ("kvs_unlink %s", key);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_future_t *f;

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
    kput (h, "a", 42);
    if (!(f = kvs_fence_begin (h, "test.asyncfence.1", 1, 0)))
        log_err_exit ("kvs_fence_begin 1");
    log_msg ("kvs_fence_begin 1");
    kput (h, "b", 43);
    if (kvs_fence_finish (f) < 0)
        log_err_exit ("kvs_fence_finish 1");
    flux_future_destroy (f);
    log_msg ("kvs_fence_finish 1");
    kget (h, "a", 42);
    kget_xfail (h, "b");
    kfence (h, "2");
    kget (h, "a", 42);
    kget (h, "b", 43);

    /* Clean up
     */
    kvs_unlink (h, "test.asyncfence");
    kcommit (h);

    flux_close (h);
    log_fini ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
