#include <string.h>
#include <errno.h>

#include "message.h"
#include "request.h"
#include "response.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_msg_t *msg;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";
    const void *d;
    const char data[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    int l, len = strlen (data);

    plan (NO_PLAN);

    /* no topic is an error */
    errno = 0;
    ok ((msg = flux_response_encode (NULL, 0, json_str)) == NULL
        && errno == EINVAL,
        "flux_response_encode returns EINVAL with no topic string");
    errno = 0;
    ok ((msg = flux_response_encode_raw (NULL, 0, data, len)) == NULL
        && errno == EINVAL,
        "flux_response_encode_raw returns EINVAL with no topic string");

    /* both errnum and payload is an error */
    errno = 0;
    ok ((msg = flux_response_encode ("foo.bar", 1, json_str)) == NULL
        && errno == EINVAL,
        "flux_response_encode returns EINVAL with both payload and errnum");
    errno = 0;
    ok ((msg = flux_response_encode_raw ("foo.bar", 1, data, len)) == NULL
        && errno == EINVAL,
        "flux_response_encode_raw returns EINVAL with both payload and errnum");

    /* without payload */
    ok ((msg = flux_response_encode ("foo.bar", 0, NULL)) != NULL,
        "flux_response_encode works with NULL payload");

    topic = NULL;
    ok (flux_response_decode (msg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "foo.bar"),
        "flux_response_decode returns encoded topic");
    ok (flux_response_decode (msg, NULL, NULL) == 0,
        "flux_response_decode topic is optional");
    errno = 0;
    ok (flux_response_decode (msg, NULL, &s) == 0 && s == NULL,
        "flux_response_decode returns s = NULL when expected payload is missing");
    flux_msg_destroy (msg);

    /* without payload (raw) */
    ok ((msg = flux_response_encode_raw ("foo.bar", 0, NULL, 0)) != NULL,
        "flux_response_encode_raw works with NULL payload");

    topic = NULL;
    ok (flux_response_decode_raw (msg, &topic, &d, &l) == 0
        && topic != NULL && !strcmp (topic, "foo.bar"),
        "flux_response_decode_raw returns encoded topic");
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0,
        "flux_response_decode_raw topic is optional");
    l = 1;
    d = (char *)&d;
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0 && l==0 && d==NULL,
        "flux_response_decode_raw returns NULL payload");
    flux_msg_destroy (msg);

    /* with json payload */
    ok ((msg = flux_response_encode ("foo.bar", 0, json_str)) != NULL,
        "flux_response_encode works with payload");

    s = NULL;
    ok (flux_response_decode (msg, NULL, &s) == 0
        && s != NULL && !strcmp (s, json_str),
        "flux_response_decode returns encoded payload");
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) == 0,
        "flux_response_decode works with payload but don't want the payload");
    flux_msg_destroy (msg);

    /* with raw payload */
    ok ((msg = flux_response_encode_raw ("foo.bar", 0, data, len)) != NULL,
        "flux_response_encode_raw works with payload");

    d = NULL;
    l = 0;
    ok (flux_response_decode_raw (msg, NULL, &d, &l) == 0
        && d != NULL && l == len && memcmp (d, data, len) == 0,
        "flux_response_decode_raw returns encoded payload");
    flux_msg_destroy (msg);

    /* with error */
    ok ((msg = flux_response_encode ("foo.bar", 42, NULL)) != NULL,
        "flux_response_encode works with errnum");
    s = NULL;
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) < 0
        && errno == 42,
        "flux_response_decode fails with encoded errnum");
    flux_msg_destroy (msg);

    /* with error (raw) */
    ok ((msg = flux_response_encode_raw ("foo.bar", 42, NULL, 0)) != NULL,
        "flux_response_encode_raw works with errnum");
    d = NULL;
    l = 0;
    errno = 0;
    ok (flux_response_decode_raw (msg, NULL, &d, &l) < 0
        && errno == 42 && d == NULL && l == 0,
        "flux_response_decode_raw fails with encoded errnum");
    flux_msg_destroy (msg);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

