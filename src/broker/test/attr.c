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
#include <sys/param.h>
#include <stdio.h>

#include "attr.h"

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

void basic (void)
{
    attr_t *attrs;
    const char *val;

    ok ((attrs = attr_create ()) != NULL,
        "attr_create works");

    /* attr_get on unknown fails
     */
    errno = 0;
    ok (attr_get (attrs, "test.foo", NULL) < 0 && errno == ENOENT,
        "attr_get on unknown attr fails with errno == ENOENT");

    /* attr_set, attr_get works
     */
    ok ((attr_set (attrs, "test.foo", "bar") == 0),
        "attr_set on known, unset attr works");
    errno = 0;
    ok (attr_get (attrs, "test.foo", NULL) == 0,
        "attr_get val=NULL works");
    val = NULL;
    ok (attr_get (attrs, "test.foo", &val) == 0 && streq (val, "bar"),
        "attr_get on new attr works returns correct val");

    /* attr_delete works
     */
    ok (attr_delete (attrs, "test.foo") == 0,
        "attr_delete works");
    errno = 0;
    ok (attr_get (attrs, "test.foo", NULL) < 0 && errno == ENOENT,
        "attr_get on deleted attr fails with errno == ENOENT");

    /* ATTR_IMMUTABLE protects against update/delete from user;
     * update/delete can NOT be forced on broker.
     */
    ok (attr_set (attrs, "test-im.foo", "baz") == 0,
        "attr_set of immutable attribute works when unset");
    val = NULL;
    ok (attr_get (attrs, "test-im.foo", &val) == 0 && streq (val, "baz"),
        "attr_get returns correct value");
    errno = 0;
    ok (attr_set (attrs, "test-im.foo", "bar") < 0 && errno == EPERM,
        "attr_set on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_set (attrs, "test-im.foo", "baz")  < 0 && errno == EPERM,
        "attr_set (force) on immutable fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "test-im.foo") < 0 && errno == EPERM,
        "attr_delete on immutable attr fails with EPERM");
    errno = 0;
    ok (attr_delete (attrs, "test-im.foo") < 0 && errno == EPERM,
        "attr_delete on immutable fails with EPERM");

    /* Add couple more attributes and exercise iterator.
     * initial hash contents: foo=bar
     */
    val = attr_first (attrs);
    ok (val && streq (val, "test-im.foo"),
        "attr_first returned test-im.foo");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");
    ok (attr_set (attrs, "test.foo1", "42") == 0
        && attr_set (attrs, "test.foo2", "43") == 0
        && attr_set (attrs, "test.foo3", "44") == 0
        && attr_set (attrs, "test.foo4", "44") == 0,
        "attr_set test.foo[1-4] works");
    val = attr_first (attrs);
    ok (val && strstarts (val, "test"),
        "attr_first returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    val = attr_next (attrs);
    ok (val && strstarts (val, "test"),
        "attr_next returned test-prefixed attr");
    ok (attr_next (attrs) == NULL,
        "attr_next returned NULL");

    attr_destroy (attrs);
}

void unknown (void)
{
    attr_t *attrs;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    errno = 0;
    ok (attr_set (attrs, "unknown", "foo") < 0 && errno == ENOENT,
        "attr_set of unknown attribute fails with ENOENT");

    attr_destroy (attrs);
}

void cmdline (void)
{
    attr_t *attrs;
    flux_error_t error;
    int rc;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    ok (attr_set_cmdline (attrs, "test.foo", "bar", &error) == 0,
        "attr_set_cmdline test.foo works");

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, "unknown", "foo", &error);
    ok (rc < 0 && errno == ENOENT,
        "attr_set_cmdline attr=unknown fails with ENOENT");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, "test-ro.foo", "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attr=test-ro.foo fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (attrs, NULL, "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attr=NULL fails with EINVAL");
    diag ("%s", error.text);

    errno = 0;
    err_init (&error);
    rc = attr_set_cmdline (NULL, "test.foo", "bar", &error);
    ok (rc < 0 && errno == EINVAL,
        "attr_set_cmdline attrs=NULL fails with EINVAL");
    diag ("%s", error.text);

    attr_destroy (attrs);
}

int test_server_thread (flux_t *h, void *arg)
{
    attr_t *attrs;
    int rc;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");
    if (attr_register_handlers (attrs, h) < 0)
        BAIL_OUT ("%s: attr_register_handlers: %s", __func__, strerror (errno));
    if (attr_set (attrs, "test.foo", "bar") < 0)
        BAIL_OUT ("%s: attr_set test.foo: %s", __func__, strerror (errno));
    if (attr_set (attrs, "test-nr.baz", "boo") < 0)
        BAIL_OUT ("%s: attr_set test-nr.baz: %s", __func__, strerror (errno));
    if (attr_set (attrs, "test-rd.x", "43") < 0)
        BAIL_OUT ("%s: attr_set test-rd.x: %s", __func__, strerror (errno));

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    if (rc < 0)
        diag ("%s: reactor failed: %s", __func__, strerror (errno));

    attr_destroy (attrs);

    return rc;
}

void handlers (flux_t *h)
{
    const char *name;
    const char *val;
    flux_error_t error;
    flux_future_t *f;

    /* Check malformed RPCs
     */
    if (!(f = flux_rpc_pack (h, "attr.rm", FLUX_NODEID_ANY, 0, "{}")))
        BAIL_OUT ("could not send attr.rm request");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "malformed attr.rm request fails with EPROTO");
    flux_future_destroy (f);
    if (!(f = flux_rpc_pack (h, "attr.get", FLUX_NODEID_ANY, 0, "{}")))
        BAIL_OUT ("could not send attr.get request");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "malformed attr.get request fails with EPROTO");
    flux_future_destroy (f);
    if (!(f = flux_rpc_pack (h, "attr.set", FLUX_NODEID_ANY, 0, "{}")))
        BAIL_OUT ("could not send attr.set request");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EPROTO,
        "malformed attr.set request fails with EPROTO");
    flux_future_destroy (f);

    /* Check unknown attribute (name does not match the static table)
     */
    errno = 0;
    ok (flux_attr_get (h, "noexist") == NULL && errno == ENOENT,
        "attr.get noexist fails with ENOENT");
    errno = 0;
    ok (flux_attr_set (h, "noexist", "42") < 0 && errno == ENOENT,
        "attr.set noexist fails with ENOENT");
    errno = 0;
    ok (flux_attr_set (h, "noexist", "42") < 0 && errno == ENOENT,
        "attr.set noexist fails with ENOENT");
    errno = 0;
    ok (flux_attr_set_ex (h, "noexist", "42", true, &error) < 0
        && errno == ENOENT,
        "the force flag doesn't help");
    diag ("%s: %s", "noexist", error.text);
    if (!(f = flux_rpc_pack (h,
                             "attr.rm",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "name", "noexist")))
        BAIL_OUT ("could not send attr.rm request");
    ok (flux_rpc_get (f, NULL) == 0,
        "attr.rm noexist works");
    flux_future_destroy (f);

    /* Check ATTR_RUNTIME
     */
    ok ((val = flux_attr_get (h, "test-nr.baz")) != NULL && streq (val, "boo"),
        "attr.get test-nr.baz works");
    errno = 0;
    ok (flux_attr_set_ex (h, "test-nr.baz", "x", false, &error) < 0
        && errno == EINVAL,
        "attr.set test-nr.baz fails with EINVAL (no ATTR_RUNTIME flag)");
    diag ("%s: %s", "test-nr.baz", error.text);
    ok (flux_attr_set_ex (h, "test-nr.baz", "x", true, NULL) == 0
        && (val = flux_attr_get (h, "test-nr.baz")) != NULL
        && streq (val, "x"),
        "but it works with the force flag");
    ok ((val = flux_attr_get (h, "test.foo")) != NULL && streq (val, "bar"),
        "attr.get test.foo works");
    ok (flux_attr_set (h, "test.foo", "y") == 0
        && (val = flux_attr_get (h, "test.foo")) != NULL
        && streq (val, "y"),
        "attr.set test.foo works (has ATTR_RUNTIME flag)");

    /* Check redirect
     */
    ok ((val = flux_attr_get (h, "test-rd.x")) != NULL && streq (val, "43"),
        "attr.get test-rd.x works");
    ok (flux_attr_set_ex (h, "test-rd.x", "44", true, &error) == 0
        && (val = flux_attr_get (h, "test-rd.x")) != NULL
        && streq (val, "44"),
        "attr.set test-rd.x (forced) works");

    /* On attr.set test-rd.*, the server tries to send a testrd.setattr
     * request, but due to the test server plumbing, that will be received
     * here, in the client.  Therefore to perform setatter we must:
     * 1) send the attr.set
     * 2) receive the testrd.setattr request
     * 3) respond to the testrd.setattr request
     * 4) receive attr.set response
     */
    flux_msg_t *req, *rep;
    const char *topic;
    ok ((f = flux_rpc_pack (h,
                            "attr.set",
                            FLUX_NODEID_ANY,
                            0,
                            "{s:s s:s}",
                            "name", "test-rd.x",
                            "value", "45")) != NULL,
        "sent attr.set test-rd.x request");
    ok ((req = flux_recv (h, FLUX_MATCH_REQUEST, 0)) != NULL
        && flux_request_unpack (req,
                                &topic,
                                "{s:s s:s}",
                                "name", &name,
                                "value", &val) == 0
        && streq (topic, "testrd.setattr"),
        "received testrd.setattr request from attr server");
    ok ((rep = flux_response_derive (req, 0)) != NULL
        && flux_send (h, rep, 0) == 0,
        "sent testrd.setattr success response");
    ok (flux_rpc_get (f, NULL) == 0,
        "received attr.set success response");
    flux_msg_decref (rep);
    flux_msg_decref (req);
    flux_future_destroy (f);

    /* Do that again but send a testrd.setattr failure response
     */
    ok ((f = flux_rpc_pack (h,
                            "attr.set",
                            FLUX_NODEID_ANY,
                            0,
                            "{s:s s:s}",
                            "name", "test-rd.x",
                            "value", "46")) != NULL,
        "sent attr.set test-rd.x request");
    ok ((req = flux_recv (h, FLUX_MATCH_REQUEST, 0)) != NULL
        && flux_request_unpack (req,
                                &topic,
                                "{s:s s:s}",
                                "name", &name,
                                "value", &val) == 0
        && streq (topic, "testrd.setattr"),
        "received testrd.setattr request from attr server");
    ok ((rep = flux_response_derive (req, EINVAL)) != NULL
        && flux_send (h, rep, 0) == 0,
        "sent testrd.setattr failure response");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == EINVAL,
        "received attr.set failed with EINVAL");
    flux_msg_decref (rep);
    flux_msg_decref (req);
    flux_future_destroy (f);
}

int main (int argc, char **argv)
{
    flux_t *h;

    plan (NO_PLAN);

    basic ();
    unknown ();
    cmdline ();

    diag ("starting test server");
    if (!(h = test_server_create (0, test_server_thread, NULL)))
        BAIL_OUT ("test_server_create failed");

    handlers (h);

    diag ("stopping test server");
    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
