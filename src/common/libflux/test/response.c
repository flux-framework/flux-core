#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    zmsg_t *zmsg, *request;
    const char *topic, *s;
    const char *json_str = "{\"a\":42}";

    plan (16);

    /* no topic is an error */
    errno = 0;
    ok ((zmsg = flux_response_encode (NULL, 0, json_str)) == NULL
        && errno == EINVAL,
        "flux_response_encode returns EINVAL with no topic string");

    /* both errnum and json_str is an error */
    errno = 0;
    ok ((zmsg = flux_response_encode ("foo.bar", 1, json_str)) == NULL
        && errno == EINVAL,
        "flux_response_encode returns EINVAL with both json and errnum");

    /* without payload */
    ok ((zmsg = flux_response_encode ("foo.bar", 0, NULL)) != NULL,
        "flux_response_encode works with NULL payload");

    topic = NULL;
    ok (flux_response_decode (zmsg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "foo.bar"),
        "flux_response_decode returns encoded topic");
    ok (flux_response_decode (zmsg, NULL, NULL) == 0,
        "flux_response_decode topic is optional");
    errno = 0;
    ok (flux_response_decode (zmsg, NULL, &s) < 0 && errno == EPROTO,
        "flux_response_decode returns EPROTO when expected payload is missing");
    zmsg_destroy(&zmsg);

    /* with payload */
    ok ((zmsg = flux_response_encode ("foo.bar", 0, json_str)) != NULL,
        "flux_response_encode works with payload");

    s = NULL;
    ok (flux_response_decode (zmsg, NULL, &s) == 0
        && s != NULL && !strcmp (s, json_str),
        "flux_response_decode returns encoded payload");
    errno = 0;
    ok (flux_response_decode (zmsg, NULL, NULL) < 0 && errno == EPROTO,
        "flux_response_decode returns EPROTO when payload is unexpected");
    zmsg_destroy (&zmsg);

    /* with error */
    ok ((zmsg = flux_response_encode ("foo.bar", 42, NULL)) != NULL,
        "flux_response_encode works with errnum");
    s = NULL;
    errno = 0;
    ok (flux_response_decode (zmsg, NULL, NULL) < 0
        && errno == 42,
        "flux_response_decode fails with encoded errnum");
    zmsg_destroy (&zmsg);

    /* from request */
    ok ((request = flux_request_encode ("foo.zzz", NULL)) != NULL,
        "flux_request_encode works");
    ok ((zmsg = flux_response_encode_ok (request, NULL)) != NULL,
        "flux_response_encode_ok works");
    ok (flux_response_decode (zmsg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "foo.zzz"),
        "flux_response_decode returns topic from request");
    zmsg_destroy (&zmsg);
    ok ((zmsg = flux_response_encode_err (request, 44)) != NULL,
        "flux_response_encode_err works");
    errno = 0;
    ok (flux_response_decode (zmsg, NULL, NULL) < 0 && errno == 44,
        "flux_response_decode returns encoded errno");
    zmsg_destroy (&request);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

