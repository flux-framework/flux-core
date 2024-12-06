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
#include <string.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_msg_t *msg;
    flux_msg_t *msg2;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";
    const void *d;
    const char data[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    size_t l;
    size_t len = strlen (data);
    int errnum;

    plan (NO_PLAN);

    /* no topic is an error */
    errno = 0;
    ok ((msg = flux_response_encode (NULL, json_str)) == NULL
        && errno == EINVAL,
        "flux_response_encode returns EINVAL with no topic string");
    errno = 0;
    ok ((msg = flux_response_encode_raw (NULL, data, len)) == NULL
        && errno == EINVAL,
        "flux_response_encode_raw returns EINVAL with no topic string");

    /* without payload */
    ok ((msg = flux_response_encode ("foo.bar", NULL)) != NULL,
        "flux_response_encode works with NULL payload");

    topic = NULL;
    ok (flux_response_decode (msg, &topic, NULL) == 0
        && topic != NULL && streq (topic, "foo.bar"),
        "flux_response_decode returns encoded topic");
    ok (flux_response_decode (msg, NULL, NULL) == 0,
        "flux_response_decode topic is optional");
    ok (flux_response_decode (msg, NULL, &s) == 0 && s == NULL,
        "flux_response_decode returns s = NULL when expected payload is missing");
    errno = 0;
    ok (flux_response_decode_error (msg, &s) < 0 && errno == ENOENT,
        "flux_response_decode_error fails with ENOENT");
    flux_msg_destroy (msg);

    /* without payload (raw) */
    ok ((msg = flux_response_encode_raw ("foo.bar", NULL, 0)) != NULL,
        "flux_response_encode_raw works with NULL payload");

    topic = NULL;
    ok (flux_response_decode_raw (msg, &topic, &d, &l) == 0
        && topic != NULL && streq (topic, "foo.bar"),
        "flux_response_decode_raw returns encoded topic");
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0,
        "flux_response_decode_raw topic is optional");
    l = 1;
    d = (char *)&d;
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0 && l==0 && d==NULL,
        "flux_response_decode_raw returns NULL payload");
    errno = 0;
    ok (flux_response_decode_error (msg, &s) < 0 && errno == ENOENT,
        "flux_response_decode_error fails with ENOENT");
    flux_msg_destroy (msg);

    /* with json payload */
    ok ((msg = flux_response_encode ("foo.bar", json_str)) != NULL,
        "flux_response_encode works with payload");

    s = NULL;
    ok (flux_response_decode (msg, NULL, &s) == 0
        && s != NULL && streq (s, json_str),
        "flux_response_decode returns encoded payload");
    ok (flux_response_decode (msg, NULL, NULL) == 0,
        "flux_response_decode works with payload but don't want the payload");
    errno = 0;
    ok (flux_response_decode_error (msg, &s) < 0 && errno == ENOENT,
        "flux_response_decode_error fails with ENOENT");
    flux_msg_destroy (msg);

    /* with raw payload */
    ok ((msg = flux_response_encode_raw ("foo.bar", data, len)) != NULL,
        "flux_response_encode_raw works with payload");

    d = NULL;
    l = 0;
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0
        && d != NULL && l == len && memcmp (d, data, len) == 0,
        "flux_response_decode_raw returns encoded payload");
    errno = 0;
    ok (flux_response_decode_error (msg, &s) < 0 && errno == ENOENT,
        "flux_response_decode_error fails with ENOENT");
    flux_msg_destroy (msg);

    /* with error */
    ok ((msg = flux_response_encode_error ("foo.bar", 42, NULL)) != NULL,
        "flux_response_encode_error works with errnum");
    s = NULL;
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) < 0
        && errno == 42,
        "flux_response_decode fails with encoded errnum");
    errno = 0;
    ok (flux_response_decode_error (msg, &s) < 0 && errno == ENOENT,
        "flux_response_decode_error fails with ENOENT");
    flux_msg_destroy (msg);

    /* with extended error message */
    ok ((msg = flux_response_encode_error ("foo.bar", 42, "My Error")) != NULL,
        "flux_response_encode_error works with errnum");
    s = NULL;
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) < 0
        && errno == 42,
        "flux_response_decode fails with encoded errnum");
    ok (flux_response_decode_error (msg, &s) == 0 && streq (s, "My Error"),
        "flux_response_decode_error includes error message");
    flux_msg_destroy (msg);

    /* response_derive with msg=NULL */
    errno = 0;
    ok (flux_response_derive (NULL, 0) == NULL && errno == EINVAL,
        "flux_response_derive msg=NULL fails with EINVAL");

    /* respond with h=NULL */
    msg = flux_request_encode ("foo", NULL);
    if (!msg)
        BAIL_OUT ("flux_request_encode failed");
    errno = 0;
    ok (flux_respond (NULL, msg, NULL) < 0 && errno == EINVAL,
        "flux_respond h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_pack (NULL, msg, "{s:i}", "x", 1) < 0 && errno == EINVAL,
        "flux_respond_pack h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_raw (NULL, msg, "foo", 3) < 0 && errno == EINVAL,
        "flux_respond_raw h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_error (NULL, msg, ENODATA, NULL) < 0 && errno == EINVAL,
        "flux_respond_error h=NULL fails with EINVAL");
    flux_msg_destroy (msg);

    /* respond with request=NULL */
    h = flux_open ("loop://", 0);
    if (!h)
        BAIL_OUT ("could not create loop handle");
    errno = 0;
    ok (flux_respond (h, NULL, NULL) < 0 && errno == EINVAL,
        "flux_respond msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_pack (h, NULL, "{s:i}", "x", 1) < 0 && errno == EINVAL,
        "flux_respond_pack msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_raw (h, NULL, "foo", 3) < 0 && errno == EINVAL,
        "flux_respond_raw msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_respond_error (h, NULL, ENODATA, NULL) < 0 && errno == EINVAL,
        "flux_respond_error msg=NULL fails with EINVAL");
    flux_close (h);

    /* errnum=0 */
    h = flux_open ("loop://", 0);
    if (!h)
        BAIL_OUT ("could not create loop handle");
    msg = flux_request_encode ("foo", NULL);
    if (!msg)
        BAIL_OUT ("flux_request_encode failed");
    ok (flux_respond_error (h, msg, 0, NULL) == 0,
        "flux_respond_error errno=0 works");
    msg2 = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (flux_msg_get_errnum (msg2, &errnum) == 0 && errnum == EINVAL,
        "and send a response message with errnum=EINVAL");
    flux_msg_destroy (msg2);
    flux_msg_destroy (msg);
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

