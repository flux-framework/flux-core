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

#include "message.h"
#include "request.h"

#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_msg_t *msg;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";
    const void *d;
    const char data[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    int i, l, len = strlen (data);

    plan (NO_PLAN);

    /* no topic is an error */
    errno = 0;
    ok ((msg = flux_request_encode (NULL, json_str)) == NULL && errno == EINVAL,
        "flux_request_encode returns EINVAL with no topic string");
    ok ((msg = flux_request_encode_raw (NULL, data, strlen (data))) == NULL
            && errno == EINVAL,
        "flux_request_encode_raw returns EINVAL with no topic string");

    /* without payload */
    ok ((msg = flux_request_encode ("foo.bar", NULL)) != NULL,
        "flux_request_encode works with NULL payload");
    topic = NULL;
    ok (flux_request_decode (msg, &topic, NULL) == 0 && topic != NULL
            && !strcmp (topic, "foo.bar"),
        "flux_request_decode returns encoded topic");
    ok (flux_request_decode (msg, NULL, NULL) == 0,
        "flux_request_decode topic is optional");
    errno = 0;
    ok (flux_request_decode (msg, NULL, &s) == 0 && s == NULL,
        "flux_request_decode returns s = NULL when expected payload is "
        "missing");
    flux_msg_destroy (msg);

    /* with JSON payload */
    ok ((msg = flux_request_encode ("foo.bar", json_str)) != NULL,
        "flux_request_encode works with payload");

    s = NULL;
    ok (flux_request_decode (msg, NULL, &s) == 0 && s != NULL
            && !strcmp (s, json_str),
        "flux_request_decode returns encoded payload");
    topic = NULL;
    i = 0;
    ok (flux_request_unpack (msg, &topic, "{s:i}", "a", &i) == 0 && i == 42
            && topic != NULL && !strcmp (topic, "foo.bar"),
        "flux_request_unpack returns encoded payload");

    errno = 0;
    ok (flux_request_decode (msg, NULL, NULL) == 0,
        "flux_request_decode works with payload but don't want the payload");
    flux_msg_destroy (msg);

    /* without payload (raw) */
    ok ((msg = flux_request_encode_raw ("foo.bar", NULL, 0)) != NULL,
        "flux_request_encode_raw works with NULL payload");
    topic = NULL;
    ok (flux_request_decode_raw (msg, &topic, &d, &l) == 0 && topic != NULL
            && !strcmp (topic, "foo.bar"),
        "flux_request_decode_raw returns encoded topic");
    ok (flux_request_decode_raw (msg, NULL, &d, &l) == 0,
        "flux_request_decode_raw topic is optional");
    d = (char *)&d;
    l = 1;
    ok (flux_request_decode_raw (msg, NULL, &d, &l) == 0 && l == 0 && d == NULL,
        "flux_request_decode_raw returned NULL payload");
    flux_msg_destroy (msg);

    /* with raw payload */
    ok ((msg = flux_request_encode_raw ("foo.bar", data, len)) != NULL,
        "flux_request_encode_raw works with payload");

    d = NULL;
    l = 0;
    ok (flux_request_decode_raw (msg, NULL, &d, &l) == 0 && d != NULL
            && l == len && memcmp (d, data, len) == 0,
        "flux_request_decode_raw returns encoded payload");
    flux_msg_destroy (msg);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
