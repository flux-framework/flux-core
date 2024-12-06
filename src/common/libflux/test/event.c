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
#include "ccan/str/str.h"

void test_codec (void)
{
    flux_msg_t *msg;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";
    const char data[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    int len = strlen (data);
    const void *d;
    size_t l;
    size_t i;

    /* no topic is an error */
    errno = 0;
    ok ((msg = flux_event_encode (NULL, json_str)) == NULL && errno == EINVAL,
        "flux_event_encode returns EINVAL with no topic string");
    errno = 0;
    ok ((msg = flux_event_encode_raw (NULL, data, len)) == NULL
        && errno == EINVAL,
        "flux_event_encode_raw topic=NULL fails with EINVAL");

    /* without payload */
    ok ((msg = flux_event_encode ("foo.bar", NULL)) != NULL,
        "flux_event_encode works with NULL payload");

    topic = NULL;
    ok (flux_event_decode (msg, &topic, NULL) == 0
        && topic != NULL && streq (topic, "foo.bar"),
        "flux_event_decode returns encoded topic");
    ok (flux_event_decode (msg, NULL, NULL) == 0,
        "flux_event_decode topic is optional");
    errno = 0;
    ok (flux_event_decode (msg, NULL, &s) == 0 && s == NULL,
        "flux_event_decode returns s = NULL when expected payload is missing");
    flux_msg_destroy(msg);

    /* with payload */
    ok ((msg = flux_event_encode ("foo.bar", json_str)) != NULL,
        "flux_event_encode works with payload");

    s = NULL;
    ok (flux_event_decode (msg, NULL, &s) == 0
        && s != NULL && streq (s, json_str),
        "flux_event_decode returns encoded payload");
    errno = 0;
    ok (flux_event_decode (msg, NULL, NULL) == 0,
        "flux_event_decode works with payload but don't want the payload");
    flux_msg_destroy (msg);

    /* formatted payload */
    ok ((msg = flux_event_pack ("foo.bar", "{s:i}", "foo", 42)) != NULL,
        "flux_event_pack packed payload object");
    i = 0;
    ok (flux_event_unpack (msg, &topic, "{s:i}", "foo", &i) == 0,
        "flux_event_unpack unpacked payload object");
    ok (i == 42 && topic != NULL && streq (topic, "foo.bar"),
        "unpacked payload matched packed");
    flux_msg_destroy (msg);

    /* raw */
    ok ((msg = flux_event_encode_raw ("foo.bar", data, len)) != NULL,
        "flux_event_encode_raw works with payload");
    d = NULL;
    l = 0;
    topic = NULL;
    ok (flux_event_decode_raw (msg, &topic, &d, &l) == 0
        && topic != NULL && streq (topic, "foo.bar")
        && d != NULL && len == len && memcmp (d, data, len) == 0,
        "flux_event_decode_raw returns encoded topic and payload");
    ok (flux_event_decode_raw (msg, NULL, &d, &l) == 0
        && d != NULL && len == len && memcmp (d, data, len) == 0,
        "flux_event_decode_raw topic=NULL returns encoded payload");

    errno = 0;
    ok (flux_event_decode_raw (msg, NULL, NULL, &l) < 0 && errno == EINVAL,
        "flux_event_decode_raw data=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_decode_raw (msg, NULL, &d, NULL) < 0 && errno == EINVAL,
        "flux_event_decode_raw len=NULL fails with EINVAL");
    flux_msg_destroy (msg);
}

void test_subscribe_badparams (void)
{
    flux_t *h;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    errno = 0;
    ok (flux_event_subscribe_ex (NULL, "foo", 0) == NULL && errno == EINVAL,
        "flux_event_subscribe_ex h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_subscribe_ex (h, NULL, 0) == NULL && errno == EINVAL,
        "flux_event_subscribe_ex topic=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_subscribe_ex (h, "foo", -1) == NULL && errno == EINVAL,
        "flux_event_subscribe_ex flags=-1 fails with EINVAL");

    errno = 0;
    ok (flux_event_unsubscribe_ex (NULL, "foo", 0) == NULL && errno == EINVAL,
        "flux_event_unsubscribe_ex h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_unsubscribe_ex (h, NULL, 0) == NULL && errno == EINVAL,
        "flux_event_unsubscribe_ex topic=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_unsubscribe_ex (h, "foo", -1) == NULL && errno == EINVAL,
        "flux_event_unsubscribe_ex flags=-1 fails with EINVAL");

    errno = 0;
    ok (flux_event_subscribe (NULL, "foo") < 0 && errno == EINVAL,
        "flux_event_subscribe h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_subscribe (h, NULL) < 0 && errno == EINVAL,
        "flux_event_subscribe topic=NULL fails with EINVAL");

    errno = 0;
    ok (flux_event_unsubscribe (NULL, "foo") < 0 && errno == EINVAL,
        "flux_event_unsubscribe h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_unsubscribe (h, NULL) < 0 && errno == EINVAL,
        "flux_event_unsubscribe topic=NULL fails with EINVAL");

    flux_close (h);
}

bool fake_failure;

void subscribe_cb (flux_t *h, flux_msg_handler_t *mh,
                   const flux_msg_t *msg, void *arg)
{
    const char *topic = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "topic", &topic) < 0)
        goto error;
    diag ("subscribe %s", topic);
    if (fake_failure) {
        errno = EIO;
        fake_failure = false;
        goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        diag ("error responding to subscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("error responding to subscribe request: %s", strerror (errno));
}

void unsubscribe_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    const char *topic = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "topic", &topic) < 0)
        goto error;
    diag ("unsubscribe %s", topic);
    if (fake_failure) {
        errno = EIO;
        fake_failure = false;
        goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        diag ("error responding to unsubscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("error responding to unsubscribe request: %s", strerror (errno));
}

const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "event.subscribe",    subscribe_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "event.unsubscribe",  unsubscribe_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int test_server (flux_t *h, void *arg)
{
    flux_msg_handler_t **handlers = NULL;
    int rc = -1;

    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        diag ("flux_msg_handler_addvec failed");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    return rc;
}

void test_subscribe_rpc (void)
{
    flux_t *h;
    flux_future_t *f;

    if (!(h = test_server_create (0, test_server, NULL)))
        BAIL_OUT ("test_server_create: %s", strerror (errno));

    ok (flux_event_subscribe (h, "fubar") == 0,
        "flux_event_subscribe topic=FUBAR works");

    ok (flux_event_unsubscribe (h, "fubar") == 0,
        "flux_event_unsubscribe topic=FUBAR works");

    fake_failure = true;
    errno = 0;
    ok (flux_event_subscribe (h, "fubar") < 0 && errno == EIO,
        "flux_event_subscribe failure works");

    fake_failure = true;
    errno = 0;
    ok (flux_event_unsubscribe (h, "fubar") < 0 && errno == EIO,
        "flux_event_unsubscribe failure works");

    f = flux_event_subscribe_ex (h, "fubar", FLUX_RPC_NORESPONSE);
    ok (f != NULL,
        "flux_event_subscribe_ex flags=FLUX_RPC_NORESPONSE works");
    flux_future_destroy (f);

    f = flux_event_unsubscribe_ex (h, "fubar", FLUX_RPC_NORESPONSE);
    ok (f != NULL,
        "flux_event_unsubscribe_ex flags=FLUX_RPC_NORESPONSE works");
    flux_future_destroy (f);

    f = flux_event_subscribe_ex (h, "fubar", 0);
    ok (f && flux_future_get (f, NULL) == 0,
        "flux_event_subscribe_ex works");
    flux_future_destroy (f);

    f = flux_event_unsubscribe_ex (h, "fubar", 0);
    ok (f && flux_future_get (f, NULL) == 0,
        "flux_event_unsubscribe_ex works");
    flux_future_destroy (f);

    fake_failure = true;
    errno = 0;
    f = flux_event_subscribe_ex (h, "fubar", 0);
    ok (f && flux_future_get (f, NULL) < 0 && errno == EIO,
        "flux_event_subscribe_ex failure works");
    flux_future_destroy (f);

    fake_failure = true;
    errno = 0;
    f = flux_event_unsubscribe_ex (h, "fubar", 0);
    ok (f && flux_future_get (f, NULL) < 0 && errno == EIO,
        "flux_event_unsubscribe_ex failure works");
    flux_future_destroy (f);

    if (test_server_stop (h) < 0)
        BAIL_OUT ("error stopping test server: %s", strerror (errno));
    flux_close (h);
}

void test_subscribe_nosub (void)
{
    flux_t *h;

    if (!(h = flux_open ("loop://", FLUX_O_TEST_NOSUB)))
        BAIL_OUT ("could not create loop handle");

    ok (flux_event_subscribe (h, "foo") == 0,
        "flux_event_subscribe succeeds in loopback with TEST_NOSUB flag");
    ok (flux_event_unsubscribe (h, "foo") == 0,
        "flux_event_unsubscribe succeeds in loopback with TEST_NOSUB flag");

    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_codec ();
    test_subscribe_badparams ();
    test_subscribe_rpc ();
    test_subscribe_nosub ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

