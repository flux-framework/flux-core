#include <errno.h>

#include "src/common/libflux/conf.h"
#include "src/common/libtap/tap.h"

void test_getput (void)
{
    flux_conf_t cf;

    ok (((cf = flux_conf_create ()) != NULL), "created conf");

    ok ((flux_conf_get (cf, "foo") == NULL && errno == ENOENT),
        "get of unknown key returns NULL (errno = ENOENT)");

    ok ((flux_conf_put (cf, "foo", "bar") == 0),
        "set a value for key");
    like (flux_conf_get (cf, "foo"), "^bar$",
        "get returns correct value");

    ok ((flux_conf_put (cf, "foo", "baz") == 0),
        "set a different value for key");
    like (flux_conf_get (cf, "foo"), "^baz$",
        "get returns new value");

    ok ((flux_conf_put (cf, "foo", NULL) == 0),
        "set NULL value for key to delete it");
    ok ((flux_conf_get (cf, "foo") == NULL && errno == ENOENT),
        "get returns NULL (errno = ENOENT)");

    ok ((flux_conf_put (cf, "a.b.c", "42") == 0),
        "set value for hierarchical key");
    like (flux_conf_get (cf, "a.b.c"), "^42$",
        "get returns correct value");
    like (flux_conf_get (cf, ".a.b.c"), "^42$",
        "get with leading path separator returns same value");

    ok ((flux_conf_get (cf, "a.b.c.") == NULL),
        "get with trailing path separator returns NULL (errno = ENONENT)");

    ok ((flux_conf_get (cf, "a.b") == NULL && errno == ENOENT),
        "get of parent 'directory' returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "a") == NULL && errno == ENOENT),
        "get of grandparent 'directory' returns NULL (errno = ENOENT)");

    ok ((flux_conf_get (cf, ".") == NULL && errno == ENOENT),
        "get of . returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "/") == NULL && errno == EINVAL),
        "get of / returns NULL (errno = EINVAL)");
    ok ((flux_conf_get (cf, "root") == NULL && errno == ENOENT),
        "get of 'root' returns NULL (errno = ENOENT)");
    ok ((flux_conf_get (cf, "") == NULL && errno == ENOENT),
        "get of '' returns NULL (errno = ENOENT)");

    ok ((flux_conf_get (cf, NULL) == NULL && errno == EINVAL),
        "get of NULL key returns NULL (errno = EINVAL)");
    ok ((flux_conf_put (cf, NULL, NULL) == -1 && errno == EINVAL),
        "put get of NULL key returns -1 (errno = EINVAL)");


    ok ((flux_conf_put (cf, "a.b.x", "43") == 0),
        "set value for secnod hierarchical key");
    like (flux_conf_get (cf, "a.b.x"), "^43$",
        "get returns correct value");

    ok ((flux_conf_put (cf, "a", NULL) == -1 && errno == EISDIR),
        "put NULL on grandparent directory returns -1 (errno = EISDIR)");
    like (flux_conf_get (cf, "a.b.c"), "^42$",
        "get of first hier key returns correct value");
    like (flux_conf_get (cf, "a.b.x"), "^43$",
        "get of second hier key returns correct value");

    flux_conf_destroy (cf);
}

void test_iterator (void)
{
    flux_conf_t cf;
    flux_conf_itr_t itr;

    ok (((cf = flux_conf_create ()) != NULL), "created conf");
    ok ((flux_conf_put (cf, "a", "x") == 0), "added first item");
    ok ((flux_conf_put (cf, "b.m.y", "y") == 0), "added second item");
    ok ((flux_conf_put (cf, "c.x", "z") == 0), "added third item");
    ok ((flux_conf_put (cf, "c.y", "Z") == 0), "added last item");

    ok (((itr = flux_conf_itr_create (cf)) != NULL), "created itr");
    like (flux_conf_next (itr), "^a$", "itr returned first item");
    like (flux_conf_next (itr), "^b.m.y$", "itr returned second item");
    like (flux_conf_next (itr), "^c.x$", "itr returned third item");
    like (flux_conf_next (itr), "^c.y$", "itr returned last item");
    ok ((flux_conf_next (itr) == NULL), "itr returned NULL");

    flux_conf_itr_destroy (itr);
    flux_conf_destroy (cf);
}

int main (int argc, char *argv[])
{
    plan (36);

    test_getput (); /* 25 tests */
    test_iterator (); /* 11 tests */

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
