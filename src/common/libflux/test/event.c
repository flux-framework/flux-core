/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <czmq.h>
#include "message.h"
#include "event.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_msg_t *msg;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";
    const char data[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    int len = strlen (data);
    const void *d;
    int l;
    int i;

    plan (NO_PLAN);

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
    ok (flux_event_decode (msg, &topic, NULL) == 0 && topic != NULL
            && !strcmp (topic, "foo.bar"),
        "flux_event_decode returns encoded topic");
    ok (flux_event_decode (msg, NULL, NULL) == 0,
        "flux_event_decode topic is optional");
    errno = 0;
    ok (flux_event_decode (msg, NULL, &s) == 0 && s == NULL,
        "flux_event_decode returns s = NULL when expected payload is missing");
    flux_msg_destroy (msg);

    /* with payload */
    ok ((msg = flux_event_encode ("foo.bar", json_str)) != NULL,
        "flux_event_encode works with payload");

    s = NULL;
    ok (flux_event_decode (msg, NULL, &s) == 0 && s != NULL
            && !strcmp (s, json_str),
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
    ok (i == 42 && topic != NULL && !strcmp (topic, "foo.bar"),
        "unpacked payload matched packed");
    flux_msg_destroy (msg);

    /* raw */
    ok ((msg = flux_event_encode_raw ("foo.bar", data, len)) != NULL,
        "flux_event_encode_raw works with payload");
    d = NULL;
    l = 0;
    topic = NULL;
    ok (flux_event_decode_raw (msg, &topic, &d, &l) == 0 && topic != NULL
            && strcmp (topic, "foo.bar") == 0 && d != NULL && len == len
            && memcmp (d, data, len) == 0,
        "flux_event_decode_raw returns encoded topic and payload");
    ok (flux_event_decode_raw (msg, NULL, &d, &l) == 0 && d != NULL
            && len == len && memcmp (d, data, len) == 0,
        "flux_event_decode_raw topic=NULL returns encoded payload");

    errno = 0;
    ok (flux_event_decode_raw (msg, NULL, NULL, &l) < 0 && errno == EINVAL,
        "flux_event_decode_raw data=NULL fails with EINVAL");
    errno = 0;
    ok (flux_event_decode_raw (msg, NULL, &d, NULL) < 0 && errno == EINVAL,
        "flux_event_decode_raw len=NULL fails with EINVAL");
    flux_msg_destroy (msg);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
