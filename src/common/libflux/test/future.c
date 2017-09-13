#include <errno.h>
#include <string.h>

#include "future.h"
#include "reactor.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int aux_destroy_called;
void *aux_destroy_arg;
void aux_destroy (void *arg)
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
void *contin_arg;
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
    contin_get_rc = flux_future_get (f, &contin_get_result);
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
    ok (f != NULL,
        "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);
    ok (flux_future_get_reactor (f) == r,
        "flux_future_get_reactor matches what was set");

    /* before aux is set */
    errno = 0;
    char *p = flux_future_aux_get (f, "foo");
    ok (p == NULL
        && errno == ENOENT,
        "flux_future_aux_get of wrong value returns ENOENT");

    /* aux */
    errno = 0;
    ok (flux_future_aux_set (f, NULL, "bar", NULL) < 0
         && errno == EINVAL,
        "flux_future_aux_set anon w/o destructor is EINVAL");
    errno = 0;
    ok (flux_future_aux_set (NULL, "foo", "bar", aux_destroy) < 0
         && errno == EINVAL,
        "flux_future_aux_set w/ NULL future is EINVAL");
    aux_destroy_called = 0;
    aux_destroy_arg = NULL;
    ok (flux_future_aux_set (f, "foo", "bar", aux_destroy) == 0,
        "flux_future_aux_set works");
    errno = 0;
    p = flux_future_aux_get (NULL, "baz");
    ok (p == NULL
        && errno == EINVAL,
        "flux_future_aux_get with bad input returns EINVAL");
    errno = 0;
    p = flux_future_aux_get (f, "baz");
    ok (p == NULL
        && errno == ENOENT,
        "flux_future_aux_get of wrong value returns ENOENT");
    p = flux_future_aux_get (f, "foo");
    ok (p != NULL && !strcmp (p, "bar"),
        "flux_future_aux_get of known returns it");
    // same value as "foo" key to not muck up destructor arg test
    ok (flux_future_aux_set (f, NULL, "bar", aux_destroy) == 0,
        "flux_future_aux_set with NULL key works");

    /* wait_for/get - no future_init; artificially call fulfill */
    errno = 0;
    ok (flux_future_wait_for (NULL, 0.) < 0 && errno == EINVAL,
        "flux_future_wait_for w/ NULL future returns EINVAL");
    errno = 0;
    ok (flux_future_wait_for (f, 0.) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for initially times out");
    errno = 0;
    void *result = NULL;
    result_destroy_called = 0;
    result_destroy_arg = NULL;
    flux_future_fulfill (f, "Hello", result_destroy);
    ok (flux_future_wait_for (f, 0.) == 0,
        "flux_future_wait_for succedes after result is set");
    ok (flux_future_get (f, &result) == 0
        && result != NULL && !strcmp (result, "Hello"),
        "flux_future_get returns correct result");
    ok (flux_future_get (f, NULL) == 0,
        "flux_future_get with NULL results argument also works");

    /* continuation (result already ready) */
    errno = 0;
    ok (flux_future_then (NULL, -1., contin, "nerp") < 0
        && errno == EINVAL,
        "flux_future_then w/ NULL future returns EINVAL");
    contin_called = 0;
    contin_arg = NULL;
    contin_get_rc = -42;
    contin_get_result = NULL;
    contin_reactor = NULL;
    ok (flux_future_then (f, -1., contin, "nerp") == 0,
        "flux_future_then registered continuation");
    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran successfully");
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

    diag ("%s: simple tests completed", __FUNCTION__);
}

void test_timeout_now (void)
{
    flux_future_t *f;

    f = flux_future_create (NULL, NULL);
    ok (f != NULL,
        "flux_future_create works");
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
    ok (f != NULL,
        "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    flux_future_set_reactor (f, r);

    ok (flux_future_then (f, 0.1, timeout_contin, &errnum) == 0,
        "flux_future_then registered continuation with timeout");
    errnum = 0;
    ok (flux_reactor_run (r, 0) == 0,
        "reactor ran successfully");
    ok (errnum == ETIMEDOUT,
        "continuation called flux_future_get and got ETIMEDOUT");

    flux_future_destroy (f);
    flux_reactor_destroy (r);

    diag ("%s: timeout works in reactor context", __FUNCTION__);
}

void simple_init_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                              int revents, void *arg)
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
    if (flux_future_aux_set (f, NULL, w,
                             (flux_free_f)flux_watcher_destroy) < 0) {
        flux_watcher_destroy (w);
        goto error;
    }
    flux_watcher_start (w);
    return;
error:
   flux_future_fulfill_error (f, errno);
}

void test_init_now (void)
{
    flux_future_t *f;
    char *result;

    f = flux_future_create (simple_init, "testarg");
    ok (f != NULL,
        "flux_future_create works");
    if (!f)
        BAIL_OUT ("flux_future_create failed");
    simple_init_called = 0;
    simple_init_arg = NULL;
    simple_init_r = NULL;
    result = NULL;
    ok (flux_future_get (f, &result) == 0,
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

char *simple_contin_result;
int simple_contin_called;
int simple_contin_rc;
void simple_contin (flux_future_t *f, void *arg)
{
    simple_contin_called++;
    simple_contin_rc = flux_future_get (f, &simple_contin_result);
}

void test_init_then (void)
{
    flux_future_t *f;
    flux_reactor_t *r;

    r = flux_reactor_create (0);
    if (!r)
        BAIL_OUT ("flux_reactor_create failed");

    f = flux_future_create (simple_init, "testarg");
    ok (f != NULL,
        "flux_future_create works");
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
    ok (flux_reactor_run (r, 0) == 0,
        "reactor successfully run");
    ok (simple_contin_called == 1,
        "continuation was called once");
    ok (simple_contin_rc == 0,
        "continuation get succeeded");
    ok (simple_contin_result != NULL
        && !strcmp (simple_contin_result, "Result!"),
        "continuation get returned correct result");

    flux_future_destroy (f);
    flux_reactor_destroy (r);

    diag ("%s: init works in reactor context", __FUNCTION__);
}


int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_simple ();
    test_timeout_now ();
    test_timeout_then ();

    test_init_now ();
    test_init_then ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

