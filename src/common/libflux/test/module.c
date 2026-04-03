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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"
#include "src/broker/module.h"

void test_debug (void)
{
    flux_t *h;
    struct flux_handle_ops ops;
    int flags;

    /* Create dummy handle with no capability - only aux hash */
    memset (&ops, 0, sizeof (ops));
    if (!(h = flux_handle_create (NULL, &ops, 0)))
        BAIL_OUT ("flux_handle_create failed");

    ok (flux_module_debug_test (h, 1, false) == false,
        "flux_module_debug_test returns false with unpopulated aux");

    if (flux_aux_set (h, "flux::debug_flags", &flags, NULL) < 0)
        BAIL_OUT ("flux_aux_set failed");

    flags = 0x0f;
    ok (flux_module_debug_test (h, 0x10, false) == false,
        "flux_module_debug_test returns false on false flag (clear=false)");
    ok (flux_module_debug_test (h, 0x01, false) == true,
        "flux_module_debug_test returns true on true flag (clear=false)");
    ok (flags == 0x0f,
        "flags are unaltered after testing with clear=false");

    ok (flux_module_debug_test (h, 0x01, true) == true,
        "flux_module_debug_test returns true on true flag (clear=true)");
    ok (flags == 0x0e,
        "flag was cleared after testing with clear=true");

    flux_handle_destroy (h);
}

void test_set_running (void)
{
    flux_t *h;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    ok (flux_module_set_running (h) == 0,
        "flux_module_set_running returns success");
    errno = 0;
    ok (flux_module_set_running (NULL) < 0 && errno == EINVAL,
        "flux_module_set_running h=NULL fails with EINVAL");

    flux_close (h);
}

void test_config_request_decode (void)
{
    flux_conf_t *conf;
    flux_msg_t *msg;
    int i;

    errno = 0;
    ok (flux_module_config_request_decode (NULL, &conf) < 0 && errno == EINVAL,
        "flux_module_config_request_decode msg=NULL fails with EINVAL");

    if (!(msg = flux_request_encode ("foo.config-reload", NULL)))
        BAIL_OUT ("could not create config-reload request");

    errno = 0;
    ok (flux_module_config_request_decode (msg, &conf) < 0 && errno == EPROTO,
        "flux_module_config_request_decode msg=no payload fails with EPROTO");

    if (flux_msg_pack (msg, "{s:i}", "foo", 42) < 0)
        BAIL_OUT ("could not add payload to config-reload request");

    conf = NULL;
    ok (flux_module_config_request_decode (msg, &conf) == 0,
        "flux_module_config_request_decode works");
    ok (conf != NULL
        && flux_conf_unpack (conf, NULL, "{s:i}", "foo", &i) == 0
        && i == 42,
        "and conf object contains the right data");
    flux_conf_decref (conf);

    ok (flux_module_config_request_decode (msg, NULL) == 0,
        "flux_module_config_request_decode conf=NULL works");

    flux_msg_decref (msg);
}

void test_module_initialize (void)
{
    flux_t *h;
    flux_error_t error;
    char *args;
    const char *val;
    flux_msg_t *msg;
    const flux_conf_t *conf;
    int rc;
    int i;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(msg = flux_request_encode ("welcome", NULL)))
        BAIL_OUT ("could not create welcome request");

    errno = 0;
    err_init (&error);
    rc = flux_module_initialize (NULL, &args, &error);
    ok (rc < 0 && errno == EINVAL,
        "flux_module_initialize h=NULL fails with EINVAL");
    diag ("%s", error.text);

    if (flux_send (h, msg, 0) < 0)
        BAIL_OUT ("could not send welcome message on loop handle");

    errno = 0;
    err_init (&error);
    rc = flux_module_initialize (h, &args, &error);
    ok (rc < 0 && errno == EPROTO,
        "flux_module_initialize on bad welcome message fails with EPROTO");
    if (rc < 0)
        diag ("%s", error.text);

    err_init (&error);
    if (flux_msg_pack (msg,
                       "{s:[ss] s:{s:s s:s} s:{s:i} s:s s:s}",
                       "args",
                         "arg1",
                         "arg2",
                       "attrs",
                         "test.hello_a", "aaa",
                         "test.hello_b", "bbb",
                       "conf",
                         "test", 99,
                       "name", "testmod",
                       "uuid", "not-a-real-uuid") < 0)
        BAIL_OUT ("could not add payload to welcome request");

    if (flux_send (h, msg, 0) < 0)
        BAIL_OUT ("could not send welcome message on loop handle");

    args = NULL;
    rc = flux_module_initialize (h, &args, &error);
    ok (rc == 0,
        "flux_module_initialize works on valid welcome message");
    if (rc < 0)
        diag ("%s", error.text);
    ok (args != NULL && streq (args, "arg1 arg2"),
        "args = \"arg1 arg2\"");
    val = flux_attr_get (h, "test.hello_a");
    ok (val != NULL && streq (val, "aaa"),
        "attr test.hello_a = aaa");
    val = flux_attr_get (h, "test.hello_b");
    ok (val != NULL && streq (val, "bbb"),
        "attr test.hello_b = bbb");
    conf = flux_get_conf (h);
    ok (conf != NULL
        && flux_conf_unpack (conf, NULL, "{s:i}", "test", &i) == 0,
        "conf test = 99");
    val = flux_aux_get (h, "flux::name");
    ok (val && streq (val, "testmod"),
        "flux::name = testmod");
    val = flux_aux_get (h, "flux::uuid");
    ok (val && streq (val, "not-a-real-uuid"),
        "flux::uuid = not-a-real-uuid");

    free (args);
    flux_msg_decref (msg);
    flux_close (h);
}

int server_cb (flux_t *h, void *arg)
{
    diag ("test server starting");

    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank attribute");
    if (flux_aux_set (h, "flux::name", "testmod", NULL) < 0)
        BAIL_OUT ("could not set flux::name aux item");
    if (flux_aux_set (h, "flux::uuid", "test-uuid", NULL) < 0)
        BAIL_OUT ("could not set flux::uuid aux item");

    ok (flux_module_register_handlers (h, NULL) == 0,
        "flux_module_register_handlers works");

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        return -1;
    diag ("test server exiting");
    return 0;
}

void test_module_register_handlers (void)
{
    int rc;
    flux_error_t error;
    flux_t *h;
    flux_msg_t *req, *rep;
    flux_future_t *f;
    const char *val;
    int flags;

    errno = 0;
    err_init (&error);
    rc = flux_module_register_handlers (NULL, &error);
    ok (rc < 0 && errno == EINVAL,
        "flux_module_register_handlers h=NULL fails with EINVAL");
    if (rc < 0)
        diag ("%s", error.text);

    /* answer subscribe request (o/w server will block) */
    if (!(h = test_server_create (0, server_cb, NULL)))
        BAIL_OUT ("could not create test server");
    req = flux_recv (h, FLUX_MATCH_REQUEST, 0);
    ok (req != NULL,
        "server sent subscribe request");
    if (!(rep = flux_response_derive (req, 0))
        || flux_send (h, rep, 0) < 0)
        BAIL_OUT ("error sending subscribe response");
    flux_msg_decref (req);
    flux_msg_decref (rep);

    /* eat module.status update from prep watcher */
    req = flux_recv (h, FLUX_MATCH_REQUEST, 0);
    ok (req != NULL,
        "server sent module.status request");
    flux_msg_decref (req);

    /* stats-get */
    val = NULL;
    ok ((f = flux_rpc (h, "testmod.stats-get", NULL, 0, 0)) != NULL
        && flux_rpc_get (f, &val) == 0,
        "testmod.stats-get works");
    if (val)
        diag ("%s", val);
    flux_future_destroy (f);

    /* stats-clear */
    val = NULL;
    ok ((f = flux_rpc (h, "testmod.stats-clear", NULL, 0, 0)) != NULL
        && flux_rpc_get (f, NULL) == 0,
        "testmod.stats-clear works");
    if (val)
        diag ("%s", val);
    flux_future_destroy (f);

    /* config-reload */
    ok ((f = flux_rpc_pack (h, "testmod.config-reload", 0, 0, "{}")) != NULL
        && flux_rpc_get (f, &val) == 0,
        "testmod.config-reload works");
    flux_future_destroy (f);

    /* config-update */
    ok ((f = flux_rpc_pack (h, "testmod.config-update", 0, 0, "{}")) != NULL
        && flux_rpc_get (f, &val) == 0,
        "testmod.config-update works");
    flux_future_destroy (f);

    /* debug */
    ok ((f = flux_rpc_pack (h,
                            "testmod.debug",
                            0,
                            0,
                            "{s:s s:i}",
                            "op", "set",
                            "flags", 42)) != NULL
        && flux_rpc_get_unpack (f, "{s:i}", "flags", &flags) == 0
        && flags == 42,
        "testmod.debug set 42 works");
    flux_future_destroy (f);
    ok ((f = flux_rpc_pack (h,
                            "testmod.debug",
                            0,
                            0,
                            "{s:s s:i}",
                            "op", "clr",
                            "flags", 0)) != NULL
        && flux_rpc_get_unpack (f, "{s:i}", "flags", &flags) == 0
        && flags == 0,
        "testmod.debug clr works");
    flux_future_destroy (f);
    ok ((f = flux_rpc_pack (h,
                            "testmod.debug",
                            0,
                            0,
                            "{s:s s:i}",
                            "op", "setbit",
                            "flags", 0x1000)) != NULL
        && flux_rpc_get_unpack (f, "{s:i}", "flags", &flags) == 0
        && flags == 0x1000,
        "testmod.debug setbit 0x1000 works");
    flux_future_destroy (f);
    ok ((f = flux_rpc_pack (h,
                            "testmod.debug",
                            0,
                            0,
                            "{s:s s:i}",
                            "op", "clrbit",
                            "flags", 0x1000)) != NULL
        && flux_rpc_get_unpack (f, "{s:i}", "flags", &flags) == 0
        && flags == 0,
        "testmod.debug clrbit 0x1000 works");
    flux_future_destroy (f);

    /* rusage */
    val = NULL;
    ok ((f = flux_rpc (h, "testmod.rusage", "{}", 0, 0)) != NULL
        && flux_rpc_get (f, &val) == 0,
        "testmod.rusage works");
    if (val)
        diag ("%s", val);
    flux_future_destroy (f);

    /* ping */
    val = NULL;
    ok ((f = flux_rpc_pack (h, "testmod.ping", 0, 0, "{}")) != NULL
        && flux_rpc_get (f, &val) == 0,
        "testmod.ping works");
    if (val)
        diag ("%s", val);
    flux_future_destroy (f);

    /* shutdown */
    ok ((f = flux_rpc (h,
                       "testmod.shutdown",
                       NULL,
                       0,
                       FLUX_RPC_NORESPONSE)) != NULL,
        "sent testmod.shutdown request");
    flux_future_destroy (f);

    test_server_stop (h);

    flux_close (h);
}

struct server2_result {
    bool exited_received;
    int exited_status;
    int exited_errnum;
};

int server2_cb (flux_t *h, void *arg)
{
    struct server2_result *result = arg;
    struct flux_match match;
    flux_msg_t *req, *rep;
    int status;
    int errnum;

    /* send a straggler request */
    if (!(req = flux_request_encode ("testmod.straggler", NULL))
        || flux_send (h, req, 0) < 0)
        BAIL_OUT ("error sending straggler request");
    flux_msg_decref (req);
    diag ("sent testmod.straggler request");

    /* handle module.status request (FINALIZING) */
    match = FLUX_MATCH_REQUEST;
    match.topic_glob = "module.status";
    req = flux_recv (h, match, 0);
    ok (req != NULL
        && flux_msg_unpack (req, "{s:i}", "status", &status) == 0
        && status == FLUX_MODSTATE_FINALIZING,
        "client sent module.status status=FINALIZING request");
    if (!(rep = flux_response_derive (req, 0))
        || flux_send (h, rep, 0) < 0)
        BAIL_OUT ("error sending module.status response");
    flux_msg_decref (rep);
    flux_msg_decref (req);

    /* receive straggler ENOSYS response */
    match = FLUX_MATCH_RESPONSE;
    match.topic_glob = "testmod.straggler";
    rep = flux_recv (h, match, 0);
    ok (rep != NULL
        && flux_msg_get_errnum (rep, &errnum) == 0
        && errnum == ENOSYS,
        "client sent ENOSYS response to straggler request");
    flux_msg_decref (rep);

    /* receive module.status request (EXITED) -- store result for main thread
     * to check after test_server_stop() joins this thread, avoiding a race
     * between this ok() and the main thread's ok() for flux_module_finalize
     * (which uses FLUX_RPC_NORESPONSE so returns before we receive EXITED).
     */
    match = FLUX_MATCH_REQUEST;
    match.topic_glob = "module.status";
    req = flux_recv (h, match, 0);
    if (req != NULL
        && flux_msg_unpack (req, "{s:i}", "status", &status) == 0
        && flux_msg_unpack (req, "{s:i}", "errnum", &errnum) == 0) {
        result->exited_received = true;
        result->exited_status = status;
        result->exited_errnum = errnum;
    }
    flux_msg_decref (req);

    return 0;
}

void test_module_finalize (void)
{
    flux_error_t error;
    int rc;
    flux_t *h;
    struct server2_result result = { 0 };

    errno = 0;
    err_init (&error);
    rc = flux_module_finalize (NULL, 0, &error);
    ok (rc < 0 && errno == EINVAL,
        "flux_module_finalize h=NULL fails with EINVAL");
    if (rc < 0)
        diag ("%s", error.text);

    if (!(h = test_server_create (0, server2_cb, &result)))
        BAIL_OUT ("could not create test server");

    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("could not set rank attribute");

    rc = flux_module_finalize (h, 42, &error);
    ok (rc == 0,
        "flux_module_finalize works");

    test_server_stop (h);

    /* Check the EXITED status after the server thread has been joined to
     * avoid a race: module_set_exited() uses FLUX_RPC_NORESPONSE so
     * flux_module_finalize() returns before the server receives EXITED.
     */
    ok (result.exited_received
        && result.exited_status == FLUX_MODSTATE_EXITED
        && result.exited_errnum == 42,
        "client sent module.status status=EXITED errnum=42 request");

    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_debug ();
    test_set_running ();
    test_config_request_decode ();
    test_module_initialize ();
    test_module_register_handlers ();
    test_module_finalize ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

