/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <string.h>
#include <czmq.h>

#include "future.h"
#include "reactor.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int aux_destroy_called;
void *aux_destroy_arg;
void aux_destroy_fun (void *arg)
{
    aux_destroy_called++;
    aux_destroy_arg = arg;
}

int result_destroy_called;
void *result_destroy_arg;
void result_destroy (void *arg)
{
    result_destroy_called = 1;
    result_destroy_arg = arg;
}

int contin_called;
const void *contin_arg;
int contin_get_rc;
flux_reactor_t *contin_reactor;
flux_t *contin_flux;
void *contin_get_result;
void contin (flux_future_t *f, void *arg)
{
    contin_called = 1;
    contin_arg = arg;
    contin_flux = flux_future_get_flux (f);
    contin_reactor = flux_future_get_reactor (f);
    contin_get_rc = flux_future_get (f, (const void **)&contin_get_result);
}

void test_simple (void)
{
    flux_future_t *f;
    flux_reactor_t *r;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");

    /* create */
    f = flux_future_create (NULL, NULL);
    ok (f != NULL, "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);
    ok (flux_future_get_reactor (f) == r,
        "flux_future_get_reactor matches what was set");

    /* before aux is set */
    errno = 0;
    char *p = flux_future_aux_get (f, "foo");
    ok (p == NULL && errno == ENOENT,
        "flux_future_aux_get of wrong value returns ENOENT");

    /* aux */
    errno = 0;
    ok (flux_future_aux_set (f, NULL, "bar", NULL) < 0 && errno == EINVAL,
        "flux_future_aux_set anon w/o destructor is EINVAL");
    errno = 0;
    ok (flux_future_aux_set (NULL, "foo", "bar", aux_destroy_fun) < 0
            && errno == EINVAL,
        "flux_future_aux_set w/ NULL future is EINVAL");
    aux_destroy_called = 0;
    aux_destroy_arg = NULL;
    ok (flux_future_aux_set (f, "foo", "bar", aux_destroy_fun) == 0,
        "flux_future_aux_set works");
    errno = 0;
    p = flux_future_aux_get (NULL, "baz");
    ok (p == NULL && errno == EINVAL,
        "flux_future_aux_get with bad input returns EINVAL");
    errno = 0;
    p = flux_future_aux_get (f, "baz");
    ok (p == NULL && errno == ENOENT,
        "flux_future_aux_get of wrong value returns ENOENT");
    p = flux_future_aux_get (f, "foo");
    ok (p != NULL && !strcmp (p, "bar"),
        "flux_future_aux_get of known returns it");
    // same value as "foo" key to not muck up destructor arg test
    ok (flux_future_aux_set (f, NULL, "bar", aux_destroy_fun) == 0,
        "flux_future_aux_set with NULL key works");

    /* is_ready/wait_for/get - no future_init; artificially call fulfill */
    errno = 0;
    ok (flux_future_wait_for (NULL, 0.) < 0 && errno == EINVAL,
        "flux_future_wait_for w/ NULL future returns EINVAL");
    errno = 0;
    ok (flux_future_wait_for (f, 0.) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for initially times out");
    ok (!flux_future_is_ready (f), "flux_future_is_ready returns false");
    errno = 0;
    const void *result = NULL;
    result_destroy_called = 0;
    result_destroy_arg = NULL;
    flux_future_fulfill (f, "Hello", result_destroy);
    ok (flux_future_wait_for (f, 0.) == 0,
        "flux_future_wait_for succedes after result is set");
    ok (flux_future_is_ready (f),
        "flux_future_is_ready returns true after result is set");
    ok (flux_future_get (f, &result) == 0 && result != NULL
            && !strcmp (result, "Hello"),
        "flux_future_get returns correct result");
    ok (flux_future_get (f, NULL) == 0,
        "flux_future_get with NULL results argument also works");

    /* continuation (result already ready) */
    errno = 0;
    ok (flux_future_then (NULL, -1., contin, "nerp") < 0 && errno == EINVAL,
        "flux_future_then w/ NULL future returns EINVAL");
    contin_called = 0;
    contin_arg = NULL;
    contin_get_rc = -42;
    contin_get_result = NULL;
    contin_reactor = NULL;
    ok (flux_future_then (f, -1., contin, "nerp") == 0,
        "flux_future_then registered continuation");
    ok (flux_reactor_run (r, 0) == 0, "reactor ran successfully");
    ok (contin_called && contin_arg != NULL && !strcmp (contin_arg, "nerp"),
        "continuation was called with correct argument");
    ok (contin_get_rc == 0 && contin_get_result != NULL
            && !strcmp (contin_get_result, "Hello"),
        "continuation obtained correct result with flux_future_get");
    ok (contin_reactor == r,
        "flux_future_get_reactor from continuation returned set reactor");

    /* destructors */
    flux_future_destroy (f);
    ok (aux_destroy_called == 2 && aux_destroy_arg != NULL
            && !strcmp (aux_destroy_arg, "bar"),
        "flux_future_destroy called aux destructor correctly");
    ok (result_destroy_called && result_destroy_arg != NULL
            && !strcmp (result_destroy_arg, "Hello"),
        "flux_future_destroy called result destructor correctly");

    flux_reactor_destroy (r);
    diag ("%s: simple tests completed", __FUNCTION__);
}

void test_timeout_now (void)
{
    flux_future_t *f;

    f = flux_future_create (NULL, NULL);
    ok (f != NULL, "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    errno = 0;
    ok (flux_future_wait_for (f, 0.1) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for timed out");
    flux_future_destroy (f);

    diag ("%s: timeout works in synchronous context", __FUNCTION__);
}

void timeout_contin (flux_future_t *f, void *arg)
{
    int *errnum = arg;
    if (flux_future_get (f, NULL) < 0)
        *errnum = errno;
}

void test_timeout_then (void)
{
    flux_future_t *f;
    flux_reactor_t *r;
    int errnum;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");

    f = flux_future_create (NULL, NULL);
    ok (f != NULL, "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    ok (flux_future_then (f, 0.1, timeout_contin, &errnum) == 0,
        "flux_future_then registered continuation with timeout");
    errnum = 0;
    ok (flux_reactor_run (r, 0) == 0, "reactor ran successfully");
    ok (errnum == ETIMEDOUT,
        "continuation called flux_future_get and got ETIMEDOUT");

    flux_future_destroy (f);
    flux_reactor_destroy (r);

    diag ("%s: timeout works in reactor context", __FUNCTION__);
}

void simple_init_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    flux_future_t *f = arg;
    flux_future_fulfill (f, "Result!", NULL);
}

int simple_init_called;
void *simple_init_arg;
flux_reactor_t *simple_init_r;
void simple_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    flux_watcher_t *w;

    simple_init_called++;
    simple_init_arg = arg;

    simple_init_r = r;
    w = flux_timer_watcher_create (r, 0.1, 0., simple_init_timer_cb, f);
    if (!w)
        goto error;
    if (flux_future_aux_set (f, NULL, w, (flux_free_f)flux_watcher_destroy)
        < 0) {
        flux_watcher_destroy (w);
        goto error;
    }
    flux_watcher_start (w);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

void test_init_now (void)
{
    flux_future_t *f;
    const char *result;

    f = flux_future_create (simple_init, "testarg");
    ok (f != NULL, "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    simple_init_called = 0;
    simple_init_arg = NULL;
    simple_init_r = NULL;
    result = NULL;
    ok (flux_future_get (f, (const void **)&result) == 0,
        "flux_future_get worked");
    ok (result != NULL && !strcmp (result, "Result!"),
        "and correct result was returned");
    ok (simple_init_called == 1 && simple_init_arg != NULL
            && !strcmp (simple_init_arg, "testarg"),
        "init was called once with correct arg");
    ok (simple_init_r != NULL,
        "flux_future_get_reactor returned tmp reactor in init");

    flux_future_destroy (f);

    diag ("%s: init works in synchronous context", __FUNCTION__);
}

const char *simple_contin_result;
int simple_contin_called;
int simple_contin_rc;
void simple_contin (flux_future_t *f, void *arg)
{
    simple_contin_called++;
    simple_contin_rc =
        flux_future_get (f, (const void **)&simple_contin_result);
}

void test_init_then (void)
{
    flux_future_t *f;
    flux_reactor_t *r;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");

    f = flux_future_create (simple_init, "testarg");
    ok (f != NULL, "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);
    simple_init_called = 0;
    simple_init_arg = &f;
    simple_init_r = NULL;
    simple_contin_result = NULL;
    simple_contin_called = 0;
    simple_contin_rc = -42;
    ok (flux_future_then (f, -1., simple_contin, NULL) == 0,
        "flux_future_then registered continuation");
    ok (simple_init_called == 1 && simple_init_arg != NULL
            && !strcmp (simple_init_arg, "testarg"),
        "init was called once with correct arg");
    ok (simple_init_r == r,
        "flux_future_get_reactor return set reactor in init");
    ok (flux_reactor_run (r, 0) == 0, "reactor successfully run");
    ok (simple_contin_called == 1, "continuation was called once");
    ok (simple_contin_rc == 0, "continuation get succeeded");
    ok (simple_contin_result != NULL
            && !strcmp (simple_contin_result, "Result!"),
        "continuation get returned correct result");

    flux_future_destroy (f);
    flux_reactor_destroy (r);

    diag ("%s: init works in reactor context", __FUNCTION__);
}

/* mumble - a 0.01s timer wrapped in a future.
 */

void mumble_timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    flux_future_t *f = arg;
    flux_future_fulfill (f, xstrdup ("Hello"), free);
}

void mumble_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    flux_watcher_t *w;
    if (!(w = flux_timer_watcher_create (r, 0.01, 0., mumble_timer_cb, f)))
        goto error;
    if (flux_future_aux_set (f, NULL, w, (flux_free_f)flux_watcher_destroy)
        < 0) {
        flux_watcher_destroy (w);
        goto error;
    }
    flux_watcher_start (w);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

flux_future_t *mumble_create (void)
{
    return flux_future_create (mumble_init, NULL);
}

int fclass_contin_rc;
void fclass_contin (flux_future_t *f, void *arg)
{
    const void **result = arg;
    fclass_contin_rc = flux_future_get (f, result);
}

void test_fclass_synchronous (char *tag, flux_future_t *f, const char *expected)
{
    const char *s;

    ok (flux_future_wait_for (f, -1.) == 0,
        "%s: flux_future_wait_for returned successfully",
        tag);
    ok (flux_future_get (f, (const void **)&s) == 0 && s != NULL
            && !strcmp (s, expected),
        "%s: flux_future_get worked",
        tag);
}

void test_fclass_asynchronous (char *tag,
                               flux_future_t *f,
                               const char *expected)
{
    flux_reactor_t *r;
    const char *s;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");

    flux_future_set_reactor (f, r);
    s = NULL;
    fclass_contin_rc = 42;
    ok (flux_future_then (f, -1., fclass_contin, &s) == 0,
        "%s: flux_future_then worked",
        tag);
    ok (flux_reactor_run (r, 0) == 0, "%s: flux_reactor_run returned", tag);
    ok (fclass_contin_rc == 0,
        "%s: continuation called flux_future_get with success",
        tag);
    ok (s != NULL && !strcmp (s, expected),
        "%s: continuation fetched expected value",
        tag);

    flux_reactor_destroy (r);
}

void test_mumble (void)
{
    flux_future_t *f;

    f = mumble_create ();
    ok (f != NULL, "mumble_create worked");
    test_fclass_synchronous ("mumble", f, "Hello");
    flux_future_destroy (f);

    f = mumble_create ();
    ok (f != NULL, "mumble_create worked");
    test_fclass_asynchronous ("mumble", f, "Hello");
    flux_future_destroy (f);
}

/* incept - two mumbles wrapped in a future, wrapped in an engima.
 * No not the last bit.
 */
struct incept {
    flux_future_t *f1;
    flux_future_t *f2;
    int count;
};
void ic_free (struct incept *ic)
{
    if (ic) {
        flux_future_destroy (ic->f1);
        flux_future_destroy (ic->f2);
        free (ic);
    }
}
struct incept *ic_alloc (void)
{
    struct incept *ic = xzmalloc (sizeof (*ic));
    ic->f1 = mumble_create ();
    ic->f2 = mumble_create ();
    if (!ic->f2 || !ic->f1) {
        ic_free (ic);
        return NULL;
    }
    return ic;
}
void incept_mumble_contin (flux_future_t *f, void *arg)
{
    flux_future_t *incept_f = arg;
    struct incept *ic = flux_future_aux_get (incept_f, "ic");
    if (ic == NULL)
        goto error;
    if (--ic->count == 0)
        flux_future_fulfill (incept_f, xstrdup ("Blorg"), free);
    return;
error:
    flux_future_fulfill_error (incept_f, errno, NULL);
}
void incept_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    struct incept *ic = arg;

    flux_future_set_reactor (ic->f1, r);
    flux_future_set_reactor (ic->f2, r);
    if (flux_future_then (ic->f1, -1., incept_mumble_contin, f) < 0)
        goto error;
    if (flux_future_then (ic->f2, -1., incept_mumble_contin, f) < 0)
        goto error;
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}
flux_future_t *incept_create (void)
{
    flux_future_t *f = NULL;
    struct incept *ic;

    if (!(ic = ic_alloc ()))
        goto error;
    if (!(f = flux_future_create (incept_init, ic))) {
        ic_free (ic);
        goto error;
    }
    if (flux_future_aux_set (f, "ic", ic, (flux_free_f)ic_free) < 0) {
        ic_free (ic);
        goto error;
    }
    ic->count = 2;
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

void test_mumble_inception (void)
{
    flux_future_t *f;

    f = incept_create ();
    ok (f != NULL, "incept_create worked");
    test_fclass_synchronous ("incept", f, "Blorg");
    flux_future_destroy (f);

    f = incept_create ();
    ok (f != NULL, "incept_create worked");
    test_fclass_asynchronous ("incept", f, "Blorg");
    flux_future_destroy (f);
}

/* walk - multiple mumbles wrapped in a future, executed serially
 * The next future is created in the current future's contination.
 */
struct walk {
    zlist_t *f;  // stack of futures
    int count;   // number of steps requested
};
void walk_free (struct walk *walk)
{
    if (walk) {
        if (walk->f) {
            flux_future_t *f;
            while ((f = zlist_pop (walk->f)))
                flux_future_destroy (f);
            zlist_destroy (&walk->f);
        }
        free (walk);
    }
}
struct walk *walk_alloc (void)
{
    struct walk *walk = xzmalloc (sizeof (*walk));
    if (!(walk->f = zlist_new ())) {
        walk_free (walk);
        return NULL;
    }
    return walk;
}
void walk_mumble_contin (flux_future_t *f, void *arg)
{
    flux_future_t *walk_f = arg;
    struct walk *walk = flux_future_aux_get (walk_f, "walk");

    if (walk == NULL)
        goto error;
    if (--walk->count > 0) {
        flux_reactor_t *r = flux_future_get_reactor (walk_f);
        flux_future_t *nf;
        if (!(nf = mumble_create ()))
            goto error;
        flux_future_set_reactor (nf, r);
        if (flux_future_then (nf, -1., walk_mumble_contin, walk_f) < 0) {
            flux_future_destroy (nf);
            goto error;
        }
        if (zlist_push (walk->f, nf) < 0) {
            flux_future_destroy (nf);
            goto error;
        }
    } else
        flux_future_fulfill (walk_f, xstrdup ("Nerg"), free);
    diag ("%s: count=%d", __FUNCTION__, walk->count);
    return;
error:
    flux_future_fulfill_error (walk_f, errno, NULL);
}
void walk_init (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    struct walk *walk = arg;

    assert (walk->count > 0);

    flux_future_t *nf;
    if (!(nf = mumble_create ()))
        goto error;
    flux_future_set_reactor (nf, r);
    if (flux_future_then (nf, -1., walk_mumble_contin, f) < 0) {
        flux_future_destroy (nf);
        goto error;
    }
    if (zlist_push (walk->f, nf) < 0) {
        flux_future_destroy (nf);
        goto error;
    }
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}
flux_future_t *walk_create (int count)
{
    flux_future_t *f = NULL;
    struct walk *walk;

    if (!(walk = walk_alloc ()))
        goto error;
    if (!(f = flux_future_create (walk_init, walk))) {
        walk_free (walk);
        goto error;
    }
    if (flux_future_aux_set (f, "walk", walk, (flux_free_f)walk_free) < 0) {
        walk_free (walk);
        goto error;
    }
    walk->count = count;
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

void test_walk (void)
{
    flux_future_t *f;

    f = walk_create (4);
    ok (f != NULL, "walk_create worked");
    test_fclass_synchronous ("walk", f, "Nerg");
    flux_future_destroy (f);

    f = walk_create (10);
    ok (f != NULL, "walk_create worked");
    test_fclass_asynchronous ("walk", f, "Nerg");
    flux_future_destroy (f);
}

void test_reset_continuation (flux_future_t *f, void *arg)
{
    int *cp = arg;
    (*cp)++;
}

void test_reset (void)
{
    flux_reactor_t *r;
    flux_future_t *f;
    int count;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    /* Check out flux_future_reset() in "now" context.
     */
    if (flux_future_wait_for (f, 0.) == 0 || errno != ETIMEDOUT)
        BAIL_OUT ("flux_future_wait_for 0. succeeded on unfulfilled future");

    flux_future_fulfill (f, NULL, NULL);
    if (flux_future_wait_for (f, 0.) < 0)
        BAIL_OUT ("flux_future_wait_for failed on fulfilled future");

    flux_future_reset (f);
    errno = 0;
    ok (flux_future_wait_for (f, 0.) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for 0. times out on reset future");

    flux_future_fulfill (f, NULL, NULL);
    ok (flux_future_wait_for (f, 0.) == 0,
        "flux_future_wait_for 0. succeeds on re-fulfilled future");

    /* Check out flux_future_reset() in "then" context.
     */
    flux_future_reset (f);
    count = 0;
    ok (flux_future_then (f, -1., test_reset_continuation, &count) == 0,
        "flux_future_then works on reset future");
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    ok (count == 0, "continuation was not called on reset future");

    flux_future_fulfill (f, NULL, NULL);
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    ok (count == 1, "continuation was called on re-fulfilled future");

    flux_future_reset (f);
    count = 0;
    ok (flux_future_then (f, -1., test_reset_continuation, &count) == 0,
        "flux_future_then works on re-reset future");
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    ok (count == 0, "continuation was not called on re-reset future");

    flux_future_fulfill (f, NULL, NULL);
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    ok (count == 1, "continuation was called on re-re-fulfilled future");

    flux_future_destroy (f);
    flux_reactor_destroy (r);
}

void test_fatal_error (void)
{
    flux_future_t *f;

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fulfill (f, "Hello", NULL);
    flux_future_fatal_error (f, EPERM, NULL);
    flux_future_fatal_error (f,
                             ENOENT,
                             NULL); /* test EPERM is not overwritten */

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "flux_future_get returns fatal error EPERM before result");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fulfill_error (f, EACCES, NULL);
    flux_future_fatal_error (f, EPERM, NULL);
    flux_future_fatal_error (f,
                             ENOENT,
                             NULL); /* test EPERM is not overwritten */

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "flux_future_get returns fatal error EPERM before result error");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fatal_error (f, EPERM, NULL);
    flux_future_fulfill (f, "Hello", NULL);

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "flux_future_get returns fatal error EPERM, later fulfillment ignored");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fatal_error (f, EPERM, NULL);
    flux_future_fulfill_error (f, EACCES, NULL);

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "flux_future_get returns fatal error EPERM, later fulfillment ignored");

    flux_future_destroy (f);
}

void fatal_error_continuation (flux_future_t *f, void *arg)
{
    int *fp = arg;
    int rc = flux_future_get (f, NULL);
    *fp = errno;
    cmp_ok (rc,
            "<",
            0,
            "flux_future_get() returns < 0 in continuation after fatal err ");
}

void test_fatal_error_async (void)
{
    int fatalerr = 0;
    flux_reactor_t *r;
    flux_future_t *f;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    flux_future_fatal_error (f, EPERM, NULL);

    ok (flux_future_then (f, -1., fatal_error_continuation, &fatalerr) == 0,
        "flux_future_then on future with fatal error");
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    cmp_ok (fatalerr, "==", EPERM, "continuation runs after fatal error");

    flux_future_destroy (f);

    fatalerr = 0;
    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    flux_future_fatal_error (f, EPERM, NULL);

    ok (flux_future_get (f, NULL) < 0 && errno == EPERM,
        "flux_future_get returns fatal error EPERM");

    ok (flux_future_then (f, -1., fatal_error_continuation, &fatalerr) == 0,
        "flux_future_then on future with fatal error and previous get");
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    cmp_ok (fatalerr,
            "==",
            EPERM,
            "continuation runs after fatal error syncrhnously retrieved");

    flux_future_destroy (f);
    flux_reactor_destroy (r);
}

void test_error_string (void)
{
    flux_future_t *f;
    const char *str;

    ok ((str = flux_future_error_string (NULL)) != NULL
            && !strcmp (str, "future NULL"),
        "flux_future_error_string returns \"future NULL\" on NULL input");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    ok ((str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "future not fulfilled"),
        "flux_future_error_string returns \"future not fulfilled\" on "
        "unfulfilled future");

    flux_future_fulfill (f, "Hello", NULL);

    ok (flux_future_get (f, NULL) == 0
            && (str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "Success"),
        "flux_future_error_string returns \"Success\" when future fulfilled "
        "with non-error result");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fulfill_error (f, ENOENT, NULL);

    ok (flux_future_get (f, NULL) < 0 && errno == ENOENT
            && (str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "No such file or directory"),
        "flux_future_error_string returns ENOENT strerror string");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fulfill_error (f, ENOENT, "foobar");

    ok (flux_future_get (f, NULL) < 0 && errno == ENOENT
            && (str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "foobar"),
        "flux_future_error_string returns correct string when error string "
        "set");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fatal_error (f, ENOENT, NULL);

    ok (flux_future_get (f, NULL) < 0 && errno == ENOENT
            && (str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "No such file or directory"),
        "flux_future_error_string returns ENOENT strerror string");

    flux_future_destroy (f);

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    flux_future_fatal_error (f, ENOENT, "boobaz");

    ok (flux_future_get (f, NULL) < 0 && errno == ENOENT
            && (str = flux_future_error_string (f)) != NULL
            && !strcmp (str, "boobaz"),
        "flux_future_error_string returns correct fatal error string "
        "when error string set");

    flux_future_destroy (f);
}

void test_multiple_fulfill (void)
{
    flux_reactor_t *r;
    flux_future_t *f;
    const void *result;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    flux_future_fulfill (f, "foo", NULL);
    flux_future_fulfill_error (f, ENOENT, NULL);
    flux_future_fulfill (f, "bar", NULL);
    flux_future_fulfill_error (f, EPERM, NULL);
    flux_future_fulfill (f, "baz", NULL);

    result = NULL;
    ok (flux_future_get (f, (const void **)&result) == 0 && result
            && !strcmp (result, "foo"),
        "flux_future_get gets fulfillment");
    flux_future_reset (f);

    ok (flux_future_get (f, (const void **)&result) < 0 && errno == ENOENT,
        "flux_future_get gets queued ENOENT error");
    flux_future_reset (f);

    result = NULL;
    ok (flux_future_get (f, (const void **)&result) == 0 && result
            && !strcmp (result, "bar"),
        "flux_future_get gets queued fulfillment");
    flux_future_reset (f);

    ok (flux_future_get (f, (const void **)&result) < 0 && errno == EPERM,
        "flux_future_get gets queued EPERM error");
    flux_future_reset (f);

    result = NULL;
    ok (flux_future_get (f, (const void **)&result) == 0 && result
            && !strcmp (result, "baz"),
        "flux_future_get gets queued fulfillment");
    flux_future_reset (f);

    flux_future_destroy (f);

    flux_reactor_destroy (r);
}

void multiple_fulfill_continuation (flux_future_t *f, void *arg)
{
    const void **resultp = arg;
    ok (flux_future_get (f, resultp) == 0,
        "flux_future_get() in async continuation works");
    flux_future_reset (f);
}

void test_multiple_fulfill_asynchronous (void)
{
    int rc;
    flux_reactor_t *r;
    flux_future_t *f;
    const void *result;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");

    if (!(f = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    flux_future_fulfill (f, "foo", NULL);
    flux_future_fulfill (f, "bar", NULL);

    /* Call continuation once to get first value and reset future */
    multiple_fulfill_continuation (f, &result);

    ok (strcmp (result, "foo") == 0,
        "calling multiple_fulfill_continuation synchronously worked");

    rc = flux_future_then (f, -1., multiple_fulfill_continuation, &result);
    cmp_ok (rc,
            "==",
            0,
            "flux_future_then() works for multiple fulfilled future");
    if (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) < 0)
        BAIL_OUT ("flux_reactor_run NOWAIT failed");
    ok (strcmp (result, "bar") == 0,
        "continuation was called for multiple-fulfilled future");

    flux_future_destroy (f);
    flux_reactor_destroy (r);
}

void test_fulfill_with (void)
{
    flux_future_t *f = NULL;
    flux_future_t *p = NULL;
    flux_future_t *x = NULL;
    const char *result = NULL;
    char *p_result = NULL;

    if (!(f = flux_future_create (NULL, NULL))
        || !(p = flux_future_create (NULL, NULL))
        || !(x = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");

    ok (flux_future_fulfill_with (NULL, NULL) < 0 && errno == EINVAL,
        "flux_future_fulfill_with (NULL, NULL) returns EINVAL");
    ok (flux_future_fulfill_with (f, NULL) < 0 && errno == EINVAL,
        "flux_future_fulfill_with (f, NULL) returns EINVAL");
    ok (flux_future_fulfill_with (NULL, f) < 0 && errno == EINVAL,
        "flux_future_fulfill_with (NULL, f) returns EINVAL");
    ok (flux_future_fulfill_with (f, p) < 0 && errno == EAGAIN,
        "flux_future_fulfill_with with unfulfilled future returns EAGAIN");

    flux_future_aux_set (p, "test", (void *)0x42, NULL);
    if (!(p_result = strdup ("result")))
        BAIL_OUT ("strdup failed");
    flux_future_fulfill (p, p_result, free);

    ok (flux_future_is_ready (p), "flux_future_t p is ready");
    ok (!flux_future_is_ready (f), "flux_future_t f is not ready");

    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with (f, p) works");

    ok (flux_future_fulfill_with (f, x) < 0 && errno == EEXIST,
        "flux_future_fulfill_with with different future returns EEXIST");

    ok (flux_future_is_ready (f), "flux_future_t f is now ready");
    ok (flux_future_get (f, (const void **)&result) == 0,
        "flux_future_get (f) works");
    ok (result == p_result && strcmp (result, "result") == 0,
        "flux_future_get (f) returns result from p");
    ok (flux_future_aux_get (f, "test") == (void *)0x42,
        "flux_future_aux_get (f, ...) retrieves aux item from p");
    flux_future_aux_set (f, "foo", (void *)0x180, NULL);
    ok (flux_future_aux_get (f, "foo") == (void *)0x180,
        "flux_future_aux_set (f) still works");

    /* Test fulfill_with when embedded future has error:
     */
    flux_future_reset (p);
    flux_future_reset (f);
    flux_future_fulfill_error (p, EFAULT, "test error string");
    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with after fulfill error works");
    ok (flux_future_is_ready (f), "f is now ready");
    ok (flux_future_get (f, NULL) < 0 && errno == EFAULT,
        "flux_future_get returns expected error and errno");
    ok (flux_future_error_string (f)
            && strcmp (flux_future_error_string (f), "test error string") == 0,
        "flux_future_error_string() has expected error string");

    /* Test fulfill_with multiple fulfillment:
     */
    flux_future_reset (p);
    flux_future_reset (f);

    flux_future_fulfill (p, (void *)0xa, NULL);
    flux_future_fulfill (p, (void *)0xb, NULL);

    ok (flux_future_is_ready (p),
        "flux_future_t p is ready with multiple fulfillment");
    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with (f, p)");

    flux_future_reset (p);
    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with (f, p) after flux_future_reset (p)");
    ok (flux_future_get (f, (const void **)&result) == 0,
        "flux_future_get (f) works");
    ok (result == (void *)0xa, "first flux_future_get returns first result");

    flux_future_reset (f);
    ok (flux_future_get (f, (const void **)&result) == 0,
        "flux_future_get (f) works");
    ok (result == (void *)0xb, "second flux_future_get returns second result");

    flux_future_reset (f);
    ok (!flux_future_is_ready (f),
        "flux_future_t f is no longer ready after reset");

    /* Test fulfill_with when p has fatal error:
     * XXX: This test should be last because you can't reset a fatal error
     */
    flux_future_reset (p);
    flux_future_reset (f);
    flux_future_fatal_error (p, EFAULT, "fatal error string");
    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with after fatal error works");
    ok (flux_future_is_ready (f), "f is now ready");
    ok (flux_future_get (f, NULL) < 0 && errno == EFAULT,
        "flux_future_get returns expected error and errno");
    ok (flux_future_error_string (f)
            && strcmp (flux_future_error_string (f), "fatal error string") == 0,
        "flux_future_error_string() has expected error string");

    flux_future_destroy (f);
    flux_future_destroy (p);
    flux_future_destroy (x);
}

void fulfill_with_continuation (flux_future_t *f, void *arg)
{
    const void **resultp = arg;
    ok (flux_future_get (f, resultp) == 0,
        "fulfill_with_async: flux_future_get works in callback");
}

void call_fulfill_with (flux_future_t *p, void *arg)
{
    flux_future_t *f = arg;
    ok (flux_future_fulfill_with (f, p) == 0,
        "flux_future_fulfill_with works in callback");
    /* flux_future_fulfill_with() takes a ref to p, so ok
     *  to destroy here.
     */
    flux_future_destroy (p);
}

void test_fulfill_with_async (void)
{
    flux_reactor_t *r = NULL;
    flux_future_t *f = NULL;
    flux_future_t *p = NULL;
    void *result = NULL;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");

    if (!(f = flux_future_create (NULL, NULL))
        || !(p = flux_future_create (NULL, NULL)))
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);
    flux_future_set_reactor (p, r);

    ok (flux_future_then (p, -1., call_fulfill_with, f) == 0,
        "flux_future_then (p, ...)");
    ok (flux_future_then (f, -1., fulfill_with_continuation, (void *)&result)
            == 0,
        "flux_future_then (f, ...)");

    flux_future_aux_set (p, "test_aux", (void *)0x42, NULL);

    // fulfill p so its continuation can fulfill f
    flux_future_fulfill (p, (void *)0xa1a1a1, NULL);

    ok (flux_reactor_run (r, 0) == 0, "flux_reactor_run");

    ok (flux_future_is_ready (f), "future f was fulfilled by p");
    ok (result == (void *)0xa1a1a1, "with result from p");
    ok (flux_future_aux_get (f, "test_aux") == (void *)0x42,
        "aux hash from future p available via future f");

    // destroys both f and embedded p
    flux_future_destroy (f);
    flux_reactor_destroy (r);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_simple ();
    test_timeout_now ();
    test_timeout_then ();

    test_init_now ();
    test_init_then ();

    test_mumble ();
    test_mumble_inception ();
    test_walk ();

    test_reset ();

    test_fatal_error ();
    test_fatal_error_async ();

    test_error_string ();

    test_multiple_fulfill ();
    test_multiple_fulfill_asynchronous ();

    test_fulfill_with ();
    test_fulfill_with_async ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
