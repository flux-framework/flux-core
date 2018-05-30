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
        && topic != NULL && !strcmp (topic, "foo.bar"),
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
        && topic != NULL && !strcmp (topic, "foo.bar"),
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
        && s != NULL && !strcmp (s, json_str),
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
    ok (flux_response_decode_error (msg, &s) == 0 && !strcmp (s, "My Error"),
        "flux_response_decode_error includes error message");
    flux_msg_destroy (msg);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

