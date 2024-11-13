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
#include "config.h"
#endif
#include <stdio.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

static bool init_and_fulfill_called = false;
static bool init_no_fulfill_called = false;

static void reset_static_sentinels (void)
{
    init_and_fulfill_called = false;
    init_no_fulfill_called = false;
}

static void init_and_fulfill (flux_future_t *f, void *arg)
{
    bool with_error = *(bool *)arg;
    init_and_fulfill_called = true;
    if (with_error)
        flux_future_fulfill_error (f, EPERM, NULL);
    else
        flux_future_fulfill (f, NULL, NULL);
}

static void init_no_fulfill (flux_future_t *f, void *arg)
{
    init_no_fulfill_called = true;
}

static void test_composite_basic_any (flux_reactor_t *r, bool with_error)
{
    flux_future_t *any = flux_future_wait_any_create ();
    flux_future_t *f1 = flux_future_create (init_no_fulfill, NULL);
    flux_future_t *f2 = flux_future_create (init_and_fulfill, &with_error);
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
    ok ((s != NULL) && streq (s, "f1"),
        "flux_future_first_child() == 'f1'");

    ok (flux_future_push (any, "f2", f2) == 0,
        "flux_future_push (any, 'f2', f2)");

    s = flux_future_first_child (any);
    ok (s != NULL && (streq (s, "f1") || streq (s, "f2")),
        "flux_future_first_child (any) returns one of two children");
    p = flux_future_next_child (any);
    ok ((p != NULL) && (streq (p, "f1") || streq (p, "f2"))
        && (!streq (p, s)),
        "flux_future_next_child (any) returns different child (%s)", s);
    ok (flux_future_next_child (any) == NULL,
        "flux_future_next_child (any) == NULL signifies end of list");

    ok (!flux_future_is_ready (any),
        "flux_future_is_ready (any) == false");

    ok (flux_future_wait_for (any, 0.1) == 0,
        "flux_future_wait_for() returns success");
    ok (init_and_fulfill_called && init_no_fulfill_called,
        "initializers for both futures called synchronously");

    if (with_error) {
        ok (flux_future_get (any, NULL) < 0 && errno == EPERM,
            "flux_future_get error if first future fulfilled with error");
    }
    else {
        ok (flux_future_get (any, NULL) == 0,
            "flux_future_get on composite returns success");
    }
    ok (!flux_future_is_ready (f1),
        "future f1 is not ready");
    ok (flux_future_is_ready (f2),
        "future f2 is ready");

    flux_future_destroy (any);
}

static void test_composite_basic_all (flux_reactor_t *r, bool with_error)
{
    flux_future_t *all = flux_future_wait_all_create ();
    flux_future_t *f1 = flux_future_create (init_no_fulfill, NULL);
    flux_future_t *f2 = flux_future_create (init_and_fulfill, &with_error);
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
    ok ((s != NULL) && streq (s, "f1"),
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

    if (with_error)
        flux_future_fulfill_error (f1, EPERM, NULL);
    else
        flux_future_fulfill (f1, NULL, NULL);

    ok (flux_future_wait_for (all, 0.1) == 0,
        "flux_future_wait_for() now returns success");

    if (with_error) {
        ok (flux_future_get (all, NULL) < 0 && errno == EPERM,
            "flux_future_get composite with error in child returns error");
    }
    else {
        ok (flux_future_get (all, NULL) == 0,
            "flux_future_get on wait_all composite returns success");
    }
    flux_future_destroy (all);
}

static void step1_or (flux_future_t *f, void *arg)
{
    const void *result;
    char *str = arg;
    /* or_then handler -- future `f` must have been fulfilled with error */
    ok (flux_future_get (f, (const void **)&result) < 0,
        "chained: step1 or_then: flux_future_get returns failure");
    strcat (str, "-step1_or");

    /* Simulate recovery, do not propagate error to next future in the chain */
    flux_future_t *next = flux_future_create (NULL, NULL);
    flux_future_continue (f, next);
    flux_future_fulfill (next, NULL, NULL);
    flux_future_destroy (f);
}

static void step2 (flux_future_t *f, void *arg)
{
    const void *result;
    char *str = arg;
    ok (flux_future_get (f, (const void **)&result) == 0,
        "chained: step2: flux_future_get returns success");
    strcat (str, "-step2");
    flux_future_t *next = flux_future_create (NULL, NULL);
    flux_future_continue (f, next);
    flux_future_fulfill (next, NULL, NULL);
    flux_future_destroy (f);
}

static void step2_err (flux_future_t *f, void *arg)
{
    const void *result;
    char *str = arg;
    ok (flux_future_get (f, (const void **)&result) == 0,
        "chained: step2: flux_future_get returns success");
    strcat (str, "-step2_err");
    flux_future_continue_error (f, 123, NULL);
    flux_future_destroy (f);
}

static void step3 (flux_future_t *f2, void *arg)
{
    const void *result;
    char *str = arg;
    ok (flux_future_get (f2, (const void **)&result) == 0,
        "chained: step3: flux_future_get returns success");
    strcat (str, "-step3");
    flux_future_t *next = flux_future_create (NULL, NULL);
    /* Set an aux member in the 'next' future here, so we can ensure
     *  we'll have access to it from the chained future later fulfilled
     *  by this one.
     */
    flux_future_aux_set (next, "test_aux", (void *) 0x42, NULL);
    flux_future_continue (f2, next);
    flux_future_fulfill (next, NULL, NULL);
    flux_future_destroy (f2);
}

static void test_basic_chained (flux_reactor_t *r)
{
    char str [4096];
    flux_future_t *f1 = NULL;
    flux_future_t *f = flux_future_create (NULL, NULL);
    flux_future_t *f2 = flux_future_and_then (f, step2, (void *) str);
    flux_future_t *f3 = flux_future_and_then (f2, step3, (void *) str);
    if (!f || !f2 || !f3)
        BAIL_OUT ("Error creating test futures");

    /*==== Basic chained future test: ====*/

    memset (str, 0, sizeof (str));
    strcat (str, "step1");

    flux_future_set_reactor (f, r);
    ok (!flux_future_is_ready (f3) && !flux_future_is_ready (f2),
        "chained: chained futures not yet ready");

    flux_future_fulfill (f, NULL, NULL);

    ok (flux_future_wait_for (f3, 0.1) == 0,
        "chained: flux_future_wait_for step3 returns");
    ok (flux_future_get (f3, NULL) == 0,
        "chained: flux_future_get == 0");
    ok (flux_future_aux_get (f3, "test_aux") == (void *) 0x42,
        "chained: aux item set in prev future available in chained future");
    is (str, "step1-step2-step3",
        "chained: futures ran in correct order");

    flux_future_destroy (f3);

    /*==== Ensure initial error is propagated to final future ===*/

    memset (str, 0, sizeof (str));
    strcat (str, "step1");

    f = flux_future_create (NULL, NULL);
    f2 = flux_future_and_then (f, step2, (void *) str);
    f3 = flux_future_and_then (f2, step3, (void *) str);
    if (!f || !f2 || !f3)
        BAIL_OUT ("Error creating test futures");

    flux_future_set_reactor (f, r);
    ok (!flux_future_is_ready (f3) && !flux_future_is_ready (f2),
        "chained: chained future not yet ready");

    flux_future_fulfill_error (f, 42, NULL);
    ok (flux_future_wait_for (f3, 0.1) == 0,
        "chained: flux_future_wait_for step3 returns 0");
    ok (flux_future_get (f3, NULL) < 0 && errno == 42,
        "chained: flux_future_get() returns -1 with errno set to errnum");

    is (str, "step1",
        "chained: no chained callbacks run by default on error");

    flux_future_destroy (f3);

    /*==== Ensure error in intermediate step is propagated through chain ====*/
    memset (str, 0, sizeof (str));
    strcat (str, "step1");

    f = flux_future_create (NULL, NULL);
    f2 = flux_future_and_then (f, step2_err, (void *) str);
    f3 = flux_future_and_then (f2, step3, (void *) str);
    if (!f || !f2 || !f3)
        BAIL_OUT ("Error creating test futures");

    flux_future_set_reactor (f, r);
    ok (!flux_future_is_ready (f3),
        "chained (failure): future not ready");

    flux_future_fulfill (f, NULL, NULL);

    ok (flux_future_wait_for (f3, 0.1) == 0,
        "chained (failure): flux_future_wait_for finished");
    ok (flux_future_is_ready (f3),
        "chained (failure): flux_future_is_ready");
    ok (flux_future_get (f3, NULL) < 0 && errno == 123,
        "chained (failure): flux_future_get: %s", strerror (errno));
    is (str, "step1-step2_err",
        "chained (failure): step2 error short-circuits step3");
    flux_future_destroy (f3);

    /*==== Recovery with flux_future_or_then() ===*/
    memset (str, 0, sizeof (str));
    strcat (str, "step1");

    f = flux_future_create (NULL, NULL);
    f2 = flux_future_and_then (f, step2, (void *) str);
    f1 = flux_future_or_then  (f, step1_or, (void *) str);
    f3 = flux_future_and_then (f2, step3, (void *) str);
    if (!f || !f1 || !f2 || !f3)
        BAIL_OUT ("Error creating test futures");

    ok (f2 == f1,
        "chained (or-then): and_then/or_then return the same 'next' future");

    flux_future_set_reactor (f, r);
    ok (!flux_future_is_ready (f3) && !flux_future_is_ready (f2),
        "chained (or-then): chained future not yet ready");

    flux_future_fulfill_error (f, 42, NULL);

    ok (flux_future_wait_for (f3, 0.1) == 0,
        "chained (or-then): flux_future_wait_for step3 returns 0");
    ok (flux_future_get (f3, NULL) == 0,
        "chained (or-then): flux_future_get() returns 0 for recovered chain");

    is (str, "step1-step1_or-step3",
        "chained (or-then): on error or_then handler called not and_then");

    flux_future_destroy (f3);
}

void timeout_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    flux_future_t *f = arg;
    flux_future_fulfill (f, NULL, NULL);
}

void timeout_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    double *dptr = arg;
    flux_watcher_t *w;
    if (!(w = flux_timer_watcher_create (r, *dptr, 0., timeout_cb, f)))
        goto error;
    /* no longer need memory for stashed argument */
    free (dptr);
    if (flux_future_aux_set (f, "watcher", w,
                             (flux_free_f) flux_watcher_destroy) < 0) {
        flux_watcher_destroy (w);
        goto error;
    }
    flux_watcher_start (w);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

static void future_timeout_clear (flux_future_t *f)
{
    flux_watcher_t *w = flux_future_aux_get (f, "watcher");
    ok (w != NULL, "timeout stop: got timer watcher");
    if (w)
        flux_watcher_stop (w);
}

static flux_future_t *future_timeout (double s)
{
    double *dptr = calloc (1, sizeof (*dptr));
    if (dptr == NULL)
        return (NULL);
    *dptr = s;
    return flux_future_create (timeout_init, (void *) dptr);
}

static int async_check_rc = -1;
void async_check (flux_future_t *fc, void *arg)
{
    bool with_error = *(bool *)arg;
    flux_future_t *f;
    ok (flux_future_is_ready (fc) == true,
        "async: composite future is ready");
    if (with_error) {
        ok (flux_future_get (fc, NULL) < 0 && errno == EPERM,
            "async: composite future fulfilled with error as expected");
        goto out;
    }
    ok ((f = flux_future_get_child (fc, "f1")) != NULL,
        "async: retrieved handle child future");
    ok (flux_future_get (f, NULL) == 0,
        "async: flux_future_get on child successful");
    ok ((f = flux_future_get_child (fc, "timeout")) != NULL,
        "async: retrieved handle to timeout future");
    ok (flux_future_get (f, NULL) == 0,
        "async: timeout future fulfilled");
out:
    async_check_rc = 0;
}

void test_composite_all_async (bool with_error)
{
    flux_reactor_t *r;
    flux_future_t *f, *fc;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");
    if (!(fc = flux_future_wait_all_create ()))
        BAIL_OUT ("flux_future_wait_all_create failed");
    if (!(f = flux_future_create (init_and_fulfill, &with_error)))
        BAIL_OUT ("flux_future_create failed");

    ok (flux_future_push (fc, "f1", f) == 0,
        "flux_future_push success");

    if (!(f = future_timeout (0.1)))
        BAIL_OUT ("future_timeout failed");

    ok (flux_future_push (fc, "timeout", f) == 0,
        "flux_future_push timeout success");

    flux_future_set_reactor (fc, r);
    ok (flux_future_then (fc, 1., async_check, &with_error) == 0,
        "flux_future_then worked");
    ok (flux_future_is_ready (fc) == 0,
        "flux_future_is_ready returns false");
    ok (flux_reactor_run (r, 0) == 0,
        "flux_reactor_run returned");
    ok (async_check_rc == 0,
        "asynchronous callback called");

    flux_future_destroy (fc);
    flux_reactor_destroy (r);
}

static int async_any_check_rc = -1;
void async_any_check (flux_future_t *fc, void *arg)
{
    bool with_error = *(bool *)arg;
    flux_future_t *f;
    ok (flux_future_is_ready (fc) == true,
        "async: composite future is ready");
    if (with_error) {
        ok (flux_future_get (fc, NULL) < 0 && errno == EPERM,
            "flux_future_get on composite returns error from child");
    }
    else {
        ok ((f = flux_future_get_child (fc, "f1")) != NULL,
            "async: retrieved handle child future");
        ok (flux_future_get (f, NULL) == 0,
            "async: flux_future_get on child successful");
    }
    ok ((f = flux_future_get_child (fc, "timeout")) != NULL,
        "async: retrieved handle to timeout future");
    ok (flux_future_is_ready (f) == false,
        "async: timeout future not yet fulfilled");
    future_timeout_clear (f);
    async_any_check_rc = 0;
    /* Required so we pop out of reactor since we will still have
     *  active watchers */
    flux_reactor_stop (flux_future_get_reactor (f));
}

void test_composite_any_async (bool with_error)
{
    flux_reactor_t *r;
    flux_future_t *f, *fc;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");
    if (!(fc = flux_future_wait_any_create ()))
        BAIL_OUT ("flux_future_wait_any_create failed");
    if (!(f = flux_future_create (init_and_fulfill, &with_error)))
        BAIL_OUT ("flux_future_create failed");

    ok (flux_future_push (fc, "f1", f) == 0,
        "flux_future_push success");

    if (!(f = future_timeout (1.0)))
        BAIL_OUT ("future_timeout failed");

    ok (flux_future_push (fc, "timeout", f) == 0,
        "flux_future_push timeout success");

    flux_future_set_reactor (fc, r);
    ok (flux_future_then (fc, -1., async_any_check, &with_error) == 0,
        "flux_future_then worked");
    ok (flux_future_is_ready (fc) == 0,
        "flux_future_is_ready returns false");
    int count = flux_reactor_run (r, 0);
    ok (count >= 0,
        "flux_reactor_run returned %d", count);
    ok (async_any_check_rc == 0,
        "asynchronous callback called");

    flux_future_destroy (fc);
    flux_reactor_destroy (r);
}

void f_strdup_init (flux_future_t *f, void *arg)
{
    char *result = strdup ((char *) arg);
    flux_future_fulfill (f, result, free);
}

void f_strcat (flux_future_t *prev, void *arg)
{
    const char *result = NULL;
    char *next = NULL;
    char *append = arg;
    flux_future_t *f;

    ok (flux_future_get (prev, (const void **)&result) == 0,
        "flux_future_get (prev) worked");
    if (asprintf (&next, "%s%s", result, append) < 0)
        BAIL_OUT ("f_strcat: asprintf: %s", strerror (errno));
    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("f_strcat: flux_future_create: %s", strerror (errno));
    flux_future_fulfill (f, next, free);
    ok (flux_future_continue (prev, f) == 0,
        "f_strcat: flux_future_continue worked");
    flux_future_destroy (prev);
}

void chained_async_cb (flux_future_t *f, void *arg)
{
    const char *result;
    const char *expected = arg;
    ok (flux_future_is_ready (f),
        "chained_async_cb: future is ready");
    ok (flux_future_get (f, (const void **) &result) == 0,
        "chained_async_cb: flux_future_get worked");
    is (result, expected,
        "chained_async_cb: got expected result");
    flux_future_destroy (f);
}

void test_chained_async ()
{
    flux_reactor_t *r;
    flux_future_t *f;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");
    if (!(f = flux_future_create (f_strdup_init, "Hello")))
        BAIL_OUT ("flux_future_create failed");
    if (!(f = flux_future_and_then (f, f_strcat, ", ")))
        BAIL_OUT ("flux_future_create failed");
    if (!(f = flux_future_and_then (f, f_strcat, "World.")))
        BAIL_OUT ("flux_future_create failed");

    flux_future_set_reactor (f, r);
    ok (flux_future_then (f, -1., chained_async_cb, "Hello, World.") == 0,
        "chained async: flux_future_then worked");
    ok (flux_reactor_run (r, 0) == 0,
        "chained async: reactor exited");
    flux_reactor_destroy (r);
}

void f_setbool (flux_future_t *prev, void *arg)
{
    bool *bp = arg;
    *bp = true;
    ok (true, "in setbool");
    flux_future_destroy (prev);
}

void test_chained_no_continue ()
{
    flux_future_t *f1, *f2, *f3;
    bool first = false;
    bool second = false;

    /*  Create empty first future to trigger auto-fulfill cascade:
     */
    if (!(f1 = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    if (!(f2 = flux_future_and_then (f1, f_setbool, &first)))
        BAIL_OUT ("flux_future_and_then");
    if (!(f3 = flux_future_and_then (f2, f_setbool, &second)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fulfill (f1, NULL, NULL);

    ok (flux_future_wait_for (f3, 5.) == 0,
        "flux_future_wait_for()");

    ok (first && second,
        "All futures auto-fulfilled without flux_future_continue");
    flux_future_destroy (f3);
}

/*  Multiple fulfill continuation. Only continue f when result is 3
 */
void check_cb (flux_future_t *f, void *arg)
{
    int *countp = arg;
    int *rp = NULL;

    ++*countp;

    if (flux_future_get (f, (void *) &rp) < 0)
        BAIL_OUT ("check_cb: flux_future_get failed");
    pass ("multiple fulfill: flux_future_get() result = %d", *rp);
    if (*rp == 3)
        ok (flux_future_continue (f, f) == 0,
            "multiple fulfill: flux_future_continue (f,f)");
    else
        ok (flux_future_continue (f, NULL) == 0,
            "multiple fulfill: flux_future_continue (f, NULL)");
    flux_future_reset (f);
}


void test_chained_multiple_fulfill ()
{
    flux_future_t *f1, *f;
    int vals[4] = { 0, 1, 2, 3 };
    int count = 0;
    int *rp = NULL;

    if (!(f1 = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    if (!(f = flux_future_and_then (f1, check_cb, &count)))
        BAIL_OUT ("flux_future_and_then");

    for (int i = 0; i < 4; i++) {
        flux_future_fulfill (f1, (void *) &vals[i], NULL);
        pass ("multiple fulfill: flux_future_fulfill (f1, %d)", vals[i]);
    }

    if (flux_future_wait_for (f, 1.) < 0)
        BAIL_OUT ("flux_future_wait_for()");

    if (flux_future_get (f, (void *) &rp) < 0)
        BAIL_OUT ("test_chained_multiple_fulfill: flux_future_get");
    ok (*rp == 3,
        "multiple fulfill: flux_future_get returned %d", *rp);
    ok (count == 4,
        "multiple fulfill: continuation called %d times", count);
    flux_future_destroy (f);

    /* NB: f1 not destroyed in its continuation since it is
     *  multiply fulfilled. Destroy it here.
     */
    flux_future_destroy (f1);
}

static void test_composite_anon_child (flux_reactor_t *r, bool with_error)
{
    flux_future_t *all = flux_future_wait_all_create ();
    flux_future_t *f1 = flux_future_create (init_no_fulfill, NULL);
    flux_future_t *f2 = flux_future_create (init_and_fulfill, &with_error);
    const char *s = NULL;
    int rc;

    if (!all || !f1 || !f2)
        BAIL_OUT ("Error creating test futures");

    reset_static_sentinels ();

    flux_future_set_reactor (all, r);

    // Test "anonymous" child futures -- future with no "name" set
    rc = flux_future_push (all, NULL, f1);
    ok (rc == 0,
        "flux_future_push (all, NULL, f1) == %d", rc);
    rc = flux_future_push (all, NULL, f2);
    ok (rc == 0,
        "flux_future_push (all, NULL, f2) == %d", rc);

    s = flux_future_first_child (all);
    ok ((s != NULL),
        "flux_future_first_child() == '%s'", s);
    s = flux_future_next_child (all);
    ok ((s != NULL),
        "flux_future_next_child() == '%s'", s);

    ok (!flux_future_is_ready (all),
        "flux_future_is_ready (all) == false");

    ok (flux_future_wait_for (all, 0.1) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for() returns ETIMEDOUT");

    ok (init_and_fulfill_called && init_no_fulfill_called,
        "initializers for both futures called synchronously");

    ok (!flux_future_is_ready (all),
        "wait_all future still not ready");

    flux_future_fulfill (f1, NULL, NULL);

    ok (flux_future_get (all, NULL) == 0,
        "flux_future_get on wait_all composite returns success");

    flux_future_destroy (all);
}

void test_empty_composite (flux_reactor_t *r)
{
    flux_future_t *all = flux_future_wait_all_create ();

    flux_future_set_reactor (all, r);
    ok (flux_future_wait_for (all, 0.1) == 0,
        "empty wait_all future is fulfilled immediately");
    ok (flux_future_get (all, NULL) == 0,
        "flux_future_get (all) == 0");
    flux_future_destroy (all);
}

static void fnext_cb (flux_future_t *f, void *arg)
{
    // Dummy continuation callback for testing below
}

void test_future_fulfill_next (flux_reactor_t *r)
{
    char *result = NULL;
    const char *s = NULL;
    flux_future_t *f = flux_future_create (NULL, NULL);
    flux_future_t *f2 = flux_future_and_then (f, fnext_cb, NULL);
    if (!f || !f2)
        BAIL_OUT ("Error creating test futures");

    ok (flux_future_fulfill_next (f2, NULL, NULL) < 0 && errno == EINVAL,
        "flux_future_fulfill_next() returns EINVAL if future is not chained");

    if (!(result = strdup ("ok")))
        BAIL_OUT ("Failed to create result");

    ok (flux_future_fulfill_next (f, result, free) == 0,
        "flux_future_fulfill_next() works on chained future");

    flux_future_destroy (f);

    ok (flux_future_wait_for (f2, 0.1) == 0,
        "next future was fulfilled by flux_future_fulfill_next()");
    ok (flux_future_get (f2, (const void **) &s) == 0,
        "flux_future_get on fulfilled future works");
    ok (s == result,
        "flux_future_get() returns expected result", s, s);

    flux_future_destroy (f2);
}

static bool issue5923_and_then_called = false;
static bool issue5923_then_called = false;

static void issue5923_and_then_cb (flux_future_t *f, void *arg)
{
    issue5923_and_then_called = false;
    flux_future_destroy (f);
}

static void issue5923_then_cb (flux_future_t *f, void *arg)
{
    issue5923_then_called = true;
    ok (flux_future_get (f, NULL) < 0 && errno == ETIMEDOUT,
        "issue5923: then cb timed out");
    flux_future_destroy (f);
}

static void test_issue_5923 (flux_reactor_t *r)
{
    flux_future_t *prev, *next;

    if (!(prev = future_timeout (0.1)))
        BAIL_OUT ("future_timeout failed");
    flux_future_set_reactor (prev, r);

    if (!(next = flux_future_and_then (prev, issue5923_and_then_cb, NULL)))
        BAIL_OUT ("flux_future_and_then failed");

    if (flux_future_then (next, 0.001, issue5923_then_cb, NULL) < 0)
        BAIL_OUT ("flux_future_then failed");

    ok (flux_reactor_run (r, 0) == 0,
        "flux_reactor_run returns 0");
    ok (issue5923_then_called,
        "issue5923: then_cb was called");
    ok (!issue5923_and_then_called,
        "issue5923: and_then_cb was not called");
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_composite_basic_any (reactor, false);
    test_composite_basic_any (reactor, true);
    test_composite_basic_all (reactor, false);
    test_composite_basic_all (reactor, true);
    test_basic_chained (reactor);
    test_composite_all_async (false);
    test_composite_all_async (true);
    test_composite_any_async (false);
    test_composite_any_async (true);
    test_chained_async ();
    test_chained_no_continue ();
    test_chained_multiple_fulfill ();

    test_composite_anon_child (reactor, false);
    test_empty_composite (reactor);

    test_future_fulfill_next (reactor);

    test_issue_5923 (reactor);

    flux_reactor_destroy (reactor);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

