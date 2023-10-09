/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libtestutil/util.h"
#include "ccan/str/str.h"
#include "src/common/librouter/servhash.h"

/*
 * Test server
 */

void service_remove_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    zhashx_t *services = arg;
    const char *topic;
    const char *service;

    if (flux_request_unpack (msg,
                             &topic,
                             "{s:s}",
                             "service", &service) < 0)
        goto error;
    diag ("%s %s", topic, service);
    if (!zhashx_lookup (services, service)) {
        errno = ENOENT;
        goto error;
    }
    zhashx_delete (services, service);
    if (flux_respond (h, msg, NULL) < 0)
        diag ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("flux_respond failed");
}

void service_add_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    zhashx_t *services = arg;
    const char *topic;
    const char *service;

    if (flux_request_unpack (msg,
                             &topic,
                             "{s:s}",
                             "service", &service) < 0)
        goto error;
    diag ("%s %s", topic, service);
    if (zhashx_lookup (services, service)) {
        errno = EEXIST;
        goto error;
    }
    zhashx_update (services, service, "foo");
    if (flux_respond (h, msg, NULL) < 0)
        diag ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("flux_respond failed");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "service.add",      service_add_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "service.remove",   service_remove_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int server_cb (flux_t *h, void *arg)
{
    flux_msg_handler_t **handlers = NULL;
    zhashx_t *services; // name => dummy-const-string

    if (!(services = zhashx_new ())) {
        diag ("zhashx_new failed");
        return -1;
    }
    if (flux_msg_handler_addvec (h, htab, services, &handlers) < 0) {
        diag ("flux_msg_handler_addvec failed");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    flux_msg_handler_delvec (handlers);
    zhashx_destroy (&services);
    return 0;
}

void test_invalid (flux_t *h)
{
    struct servhash *sh;
    flux_msg_t *msg;
    const char *uuid;

    if (!(sh = servhash_create (h)))
        BAIL_OUT ("servhash_create failed");
    if (!(msg = flux_request_encode ("foo.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    errno = 0;
    ok (servhash_create (NULL) == NULL && errno == EINVAL,
        "servhash_create h=NULL fails with EINVAL");

    errno = 0;
    ok (servhash_match (NULL, msg, &uuid) < 0 && errno == EINVAL,
        "servhash_match sh=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_match (sh, NULL, &uuid) < 0 && errno == EINVAL,
        "servhash_match msg=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_match (sh, msg, NULL) < 0 && errno == EINVAL,
        "servhash_match uuid=NULL fails with EINVAL");

    errno = 0;
    ok (servhash_add (NULL, "foo", "uuid", msg) < 0 && errno == EINVAL,
        "servhash_add sh=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_add (sh, NULL, "uuid", msg) < 0 && errno == EINVAL,
        "servhash_add name=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_add (sh, "foo", NULL, msg) < 0 && errno == EINVAL,
        "servhash_add uuid=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_add (sh, "foo", "uuid", NULL) < 0 && errno == EINVAL,
        "servhash_add msg=NULL fails with EINVAL");

    errno = 0;
    ok (servhash_remove (NULL, "foo", "uuid", msg) < 0 && errno == EINVAL,
        "servhash_remove sh=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_remove (sh, NULL, "uuid", msg) < 0 && errno == EINVAL,
        "servhash_remove name=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_remove (sh, "foo", NULL, msg) < 0 && errno == EINVAL,
        "servhash_remove uuid=NULL fails with EINVAL");
    errno = 0;
    ok (servhash_remove (sh, "foo", "uuid", NULL) < 0 && errno == EINVAL,
        "servhash_remove msg=NULL fails with EINVAL");

    flux_msg_destroy (msg);
    servhash_destroy (sh);
}

int last_errnum;

void respond_cb (const flux_msg_t *msg, const char *uuid, int errnum, void *arg)
{
    flux_reactor_t *r = arg;
    diag ("respond %s errnum=%d", uuid, errnum);
    last_errnum = errnum;
    flux_reactor_stop (r);
}

void test_basic (flux_t *h)
{
    struct servhash *sh;
    flux_msg_t *add;
    flux_msg_t *remove;
    flux_msg_t *req;
    flux_msg_t *req2;
    flux_reactor_t *r;
    const char *uuid;
    flux_future_t *f;

    if (!(add = flux_request_encode ("service.add", NULL))
            || flux_msg_pack (add, "{s:s}", "service", "fubar") < 0)
        BAIL_OUT ("request encode failed");
    if (!(remove = flux_request_encode ("service.remove", NULL))
            || flux_msg_pack (remove, "{s:s}", "service", "fubar") < 0)
        BAIL_OUT ("request encode failed");
    if (!(req = flux_request_encode ("fubar.baz", NULL)))
        BAIL_OUT ("request encode failed");
    if (!(req2 = flux_request_encode ("bleah.bar", NULL)))
        BAIL_OUT ("request encode failed");

    sh = servhash_create (h);
    ok (sh != NULL,
        "servhash_create works");
    r = flux_get_reactor (h);
    servhash_set_respond (sh, respond_cb, r);

    /* add 'fubar' */
    ok (servhash_add (sh, "fubar", "basic-uuid", add) == 0,
        "servhash_add sent add request");
    last_errnum = 42;
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run processed a response");
    ok (last_errnum == 0,
        "add request was successful");

    /* try to add 'fubar' again */
    errno = 0;
    ok (servhash_add (sh, "fubar", "basic-uuid2", add) < 0
        && errno == EEXIST,
        "servhash_add for same service failed with EEXIST");

    /* servhash_renew makes a synchronous RPC internally for any existing
     * services.  The service thread should respond with EEXIST.
     */
    errno = 0;
    ok (servhash_renew (sh) < 0 && errno == EEXIST,
        "servhash_renew fails with EEXIST");

    /* remove the service with a direct rpc, then call servhash_renew()
     * to restore it.
     */
    if (!(f = flux_rpc_message (h, remove, FLUX_NODEID_ANY, 0))
        || flux_rpc_get (f, NULL) < 0)
        BAIL_OUT ("error removing fubar with direct RPC");
    flux_future_destroy (f);
    ok (servhash_renew (sh) == 0,
        "servhash_renew works");

    /* match some messages */
    uuid = NULL;
    ok (servhash_match (sh, req, &uuid) == 0,
        "servhash_match matched request");
    ok (uuid != NULL && streq (uuid, "basic-uuid"),
        "and matched it to the correct uuid");
    errno = 0;
    ok (servhash_match (sh, req2, &uuid) < 0 && errno == ENOENT,
        "servhash_match rejected unregistered request");

    /* remove 'fubar' */
    ok (servhash_remove (sh, "fubar", "basic-uuid", remove) == 0,
        "servhash_remove sent request");
    last_errnum = 42;
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run processed a response");
    ok (last_errnum == 0,
        "remove request was successful");

    /* renew with no valid services is a no-op */
    ok (servhash_renew (sh) == 0,
        "servhash_renew works with empty servhash");

    flux_msg_destroy (add);
    flux_msg_destroy (remove);
    flux_msg_destroy (req);
    flux_msg_destroy (req2);
    servhash_destroy (sh);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    diag ("starting test server");

    if (!(h = test_server_create (0, server_cb, NULL)))
        BAIL_OUT ("test_server_create failed");

    test_basic (h);
    test_invalid (h);

    diag ("stopping test server");
    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);
    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
