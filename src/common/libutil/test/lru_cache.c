#include <errno.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/lru_cache.h"
#include "src/common/libutil/xzmalloc.h"

void test_basic ()
{
    int i;
    const int size = 5;
    const int nputs = size*2;
    lru_cache_t *lru;

    lru = lru_cache_create (size);
    ok (lru != NULL, "lru_cache_create (%d)", size);
    ok (lru_cache_size (lru) == 0, "lru_cache_size == 0");

    lru_cache_set_free_f (lru, free);

    for (i = 0; i < nputs; i++) {
        char *key = xasprintf ("%d", i);
        int *ip = xzmalloc (sizeof (*ip));
        *ip = i;
        ok (lru_cache_put (lru, key, ip) == 0, "lru_cache_put (%s)", key);
        ok (lru_cache_check (lru, key), "lru_cache_check (%s)", key);
        free (key);
        if (i >= 4) /* keep entry 0 "hot" */
            lru_cache_get (lru, "0");
    }
    ok (lru_cache_size (lru) == size,
        "lru_cache_size still %d after %d puts", size, nputs);
    ok (lru_cache_get (lru, "0") != NULL, "0 still cached");
    ok (lru_cache_put (lru, "0", NULL) == -1 && errno == EEXIST,
        "lru_cache_put on existing key returns -1");
    ok (lru_cache_get (lru, "6") != NULL, "6 still cached");
    ok (lru_cache_get (lru, "7") != NULL, "7 still cached");
    ok (lru_cache_get (lru, "8") != NULL, "8 still cached");
    ok (lru_cache_get (lru, "9") != NULL, "9 still cached");
    ok (lru_cache_get (lru, "5") == NULL, "5 not cached");

    ok (lru_cache_get (lru, "9") != NULL, "second get worked");

    ok (lru_cache_remove (lru, "0") >= 0, "lru_cache_remove ()");
    ok (lru_cache_get (lru, "0") == NULL, "remove worked");
    ok (lru_cache_size (lru) == (size-1),
        "cache size %d after remove", size - 1);

    lru_cache_destroy (lru);
}

void fake_int_free (int *iptr)
{
    /*  Note this has been "freed" by setting to -1 */
    *iptr = -1;
}

void test_free_fn ()
{
    lru_cache_t *lru;
    int x = 1, y = 2, z = 3;

    lru = lru_cache_create (2);
    lru_cache_set_free_f (lru, (lru_cache_free_f) fake_int_free);

    ok (lru_cache_put (lru, "x", &x) == 0, "lru_cache_put (x)");
    ok (lru_cache_put (lru, "y", &y) == 0, "lru_cache_put (y)");
    ok (lru_cache_put (lru, "z", &z) == 0, "lru_cache_put (z)");

    ok (lru_cache_check (lru, "x") == false, "lru_cache_check (x) is false");
    ok (x == -1, "x has been freed");
    ok (y == 2,  "y is not freed");
    ok (z == 3,  "z is not freed");

    lru_cache_destroy (lru);

    ok (y == -1, "y is now freed");
    ok (z == -1, "z is now freed");
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);
    test_basic ();
    test_free_fn ();
    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
