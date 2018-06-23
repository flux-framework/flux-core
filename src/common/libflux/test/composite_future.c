#include <czmq.h>

#include "src/common/libflux/reactor.h"
#include "src/common/libflux/future.h"
#include "src/common/libtap/tap.h"

static bool init_and_fulfill_called = false;
static bool init_no_fulfill_called = false;

static void reset_static_sentinels (void)
{
    init_and_fulfill_called = false;
    init_no_fulfill_called = false;
}

static void init_and_fulfill (flux_future_t *f, void *arg)
{
    init_and_fulfill_called = true;
    flux_future_fulfill (f, NULL, NULL);
}

static void init_no_fulfill (flux_future_t *f, void *arg)
{
    init_no_fulfill_called = true;
}

static void test_composite_basic_any (flux_reactor_t *r)
{
    flux_future_t *any = flux_future_wait_any_create ();
    flux_future_t *f1 = flux_future_create (init_no_fulfill, NULL);
    flux_future_t *f2 = flux_future_create (init_and_fulfill, NULL);
    const char *s = NULL;
    const char *p = NULL;
    int rc;

    if (!any || !f1 || !f2)
        BAIL_OUT ("Error creating test futures");

    flux_future_set_reactor (any, r);

    ok (flux_future_push (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_future_push (NULL, NULL, NULL) returns EINVAL");
    ok (flux_future_push (any, NULL, NULL) < 0 && errno == EINVAL,
        "flux_future_push (any, NULL, NULL) returns EINVAL");
    ok (flux_future_push (any, NULL, f1) < 0 && errno == EINVAL,
        "flux_future_push (any, NULL, f1) returns EINVAL");
    ok (flux_future_push (f1, "any", any) < 0 && errno == EINVAL,
        "flux_future_push on non-composite future returns EINVAL");

    ok (flux_future_first_child (any) == NULL,
        "flux_future_first_child with no children returns NULL");
    ok (flux_future_get_child (any, "foo") == NULL,
        "flux_future_get_child (any, 'foo') == NULL");
    rc = flux_future_push (any, "f1", f1);
    ok (rc == 0,
        "flux_future_push (any, 'f1', f1) == %d", rc);
    ok (flux_future_get_child (any, "f1") == f1,
        "flux_future_get_child (any, 'f1') == f1");

    s = flux_future_first_child (any);
    ok ((s != NULL) && !strcmp (s, "f1"),
        "flux_future_first_child() == 'f1'");

    ok (flux_future_push (any, "f2", f2) == 0,
        "flux_future_push (any, 'f2', f2)");

    s = flux_future_first_child (any);
    ok (s != NULL && (!strcmp (s, "f1") || !strcmp (s, "f2")),
        "flux_future_first_child (any) returns one of two children");
    p = flux_future_next_child (any);
    ok ((p != NULL) && (!strcmp (p, "f1") || !strcmp (p, "f2"))
        && (strcmp (p, s) != 0),
        "flux_future_next_child (any) returns different child (%s)", s);
    ok (flux_future_next_child (any) == NULL,
        "flux_future_next_child (any) == NULL signifies end of list");

    ok (!flux_future_is_ready (any),
        "flux_future_is_ready (any) == false");

    ok (flux_future_wait_for (any, 0.1) == 0,
        "flux_future_wait_for() returns success");
    ok (init_and_fulfill_called && init_no_fulfill_called,
        "initializers for both futures called synchronously");
    ok (flux_future_get (any, NULL) == 0,
        "flux_future_get on composite returns success");
    ok (!flux_future_is_ready (f1),
        "future f1 is not ready");
    ok (flux_future_is_ready (f2),
        "future f2 is ready");

    flux_future_destroy (any);
}

static void test_composite_basic_all (flux_reactor_t *r)
{
    flux_future_t *all = flux_future_wait_all_create ();
    flux_future_t *f1 = flux_future_create (init_no_fulfill, NULL);
    flux_future_t *f2 = flux_future_create (init_and_fulfill, NULL);
    const char *s = NULL;
    int rc;

    if (!all || !f1 || !f2)
        BAIL_OUT ("Error creating test futures");

    reset_static_sentinels ();

    flux_future_set_reactor (all, r);

    rc = flux_future_push (all, "f1", f1);
    ok (rc == 0,
        "flux_future_push (all, 'f1', f1) == %d", rc);
    ok (flux_future_get_child (all, "f1") == f1,
        "flux_future_get_child (all, 'f1') == f1");

    s = flux_future_first_child (all);
    ok ((s != NULL) && !strcmp (s, "f1"),
        "flux_future_first_child() == 'f1'");

    ok (flux_future_push (all, "f2", f2) == 0,
        "flux_future_push (all, 'f2', f2)");

    ok (!flux_future_is_ready (all),
        "flux_future_is_ready (all) == false");

    ok (flux_future_wait_for (all, 0.1) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for() returns ETIMEDOUT");

    ok (init_and_fulfill_called && init_no_fulfill_called,
        "initializers for both futures called synchronously");

    ok (!flux_future_is_ready (all),
        "wait_all future still not ready");

    flux_future_fulfill (f1, NULL, NULL);

    ok (flux_future_wait_for (all, 0.1) == 0,
        "flux_future_wait_for() now returns success");

    ok (flux_future_get (all, NULL) == 0,
        "flux_future_get on wait_all composite returns success");

    flux_future_destroy (all);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_composite_basic_any (reactor);
    test_composite_basic_all (reactor);

    flux_reactor_destroy (reactor);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

