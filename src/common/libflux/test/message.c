#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <errno.h>

#include "src/common/libflux/message.h"
#include "src/common/libtap/tap.h"

/* flux_msg_get_route_first, flux_msg_get_route_last, _get_route_count
 *   on message with variable number of routing frames
 */
void check_routes (void)
{
    flux_msg_t *msg;
    char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
        && flux_msg_frames (msg) == 1,
        "flux_msg_create works and creates msg with 1 frame");
    errno = 0;
    ok (flux_msg_get_route_count (msg) < 0 && errno == EPROTO,
        "flux_msg_get_route_count returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_first (msg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_first returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_last (msg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_last returns -1 errno EPROTO on msg w/o delim");
    ok ((flux_msg_pop_route (msg, &s) == -1 && errno == EPROTO),
        "flux_msg_pop_route returns -1 errno EPROTO on msg w/o delim");

    ok (flux_msg_clear_route (msg) == 0 && flux_msg_frames (msg) == 1,
        "flux_msg_clear_route works, is no-op on msg w/o delim");
    ok (flux_msg_enable_route (msg) == 0 && flux_msg_frames (msg) == 2,
        "flux_msg_enable_route works, adds one frame on msg w/o delim");
    ok ((flux_msg_get_route_count (msg) == 0),
        "flux_msg_get_route_count returns 0 on msg w/delim");
    ok (flux_msg_pop_route (msg, &s) == 0 && s == NULL,
        "flux_msg_pop_route works and sets id to NULL on msg w/o routes");

    ok (flux_msg_get_route_first (msg, &s) == 0 && s == NULL,
        "flux_msg_get_route_first returns 0, id=NULL on msg w/delim");
    ok (flux_msg_get_route_last (msg, &s) == 0 && s == NULL,
        "flux_msg_get_route_last returns 0, id=NULL on msg w/delim");
    ok (flux_msg_push_route (msg, "sender") == 0 && flux_msg_frames (msg) == 3,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (msg) == 1),
        "flux_msg_get_route_count returns 1 on msg w/delim+id");

    ok (flux_msg_get_route_first (msg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_get_route_last (msg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "sender",
        "flux_msg_get_route_last returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_push_route (msg, "router") == 0 && flux_msg_frames (msg) == 4,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (msg) == 2),
        "flux_msg_get_route_count returns 2 on msg w/delim+id1+id2");

    ok (flux_msg_get_route_first (msg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id1 on msg w/delim+id1+id2");
    free (s);

    ok (flux_msg_get_route_last (msg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "router",
        "flux_msg_get_route_last returns id2 on message with delim+id1+id2");
    free (s);

    s = NULL;
    ok (flux_msg_pop_route (msg, &s) == 0 && s != NULL,
        "flux_msg_pop_route works on msg w/routes");
    like (s, "router",
        "flux_msg_pop_routet returns id2 on message with delim+id1+id2");
    free (s);

    ok (flux_msg_clear_route (msg) == 0 && flux_msg_frames (msg) == 1,
        "flux_msg_clear_route strips routing frames and delim");
    flux_msg_destroy (msg);
}

/* flux_msg_get_topic, flux_msg_set_topic on message with and without routes
 */
void check_topic (void)
{
    flux_msg_t *msg;
    const char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    errno = 0;
    ok (flux_msg_get_topic (msg, &s) < 0 && errno == EPROTO,
       "flux_msg_get_topic fails with EPROTO on msg w/o topic");
    ok (flux_msg_set_topic (msg, "blorg") == 0,
       "flux_msg_set_topic works");
    ok (flux_msg_get_topic (msg, &s) == 0,
       "flux_msg_get_topic works on msg w/topic");
    like (s, "blorg",
       "and we got back the topic string we set");

    ok (flux_msg_enable_route (msg) == 0,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (msg, "id1") == 0,
        "flux_msg_push_route works");
    ok (flux_msg_get_topic (msg, &s) == 0,
       "flux_msg_get_topic still works, with routes");
    like (s, "blorg",
       "and we got back the topic string we set");
    flux_msg_destroy (msg);
}

void check_payload_json (void)
{
    const char *s;
    flux_msg_t *msg;
    const char *json_str = "{\"foo\"=42}";

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");

    s = (char *)msg;
    ok (flux_msg_get_json (msg, &s) == 0 && s == NULL,
       "flux_msg_get_json returns success with no payload");

    /* RFC 3 - json payload must be an object
     * Encoding should return EINVAL.
     */
    errno = 0;
    ok (flux_msg_set_json (msg, "[1,2,3]") < 0 && errno == EINVAL,
       "flux_msg_set_json array fails with EINVAL");
    errno = 0;
    ok (flux_msg_set_json (msg, "3.14") < 0 && errno == EINVAL,
       "flux_msg_set_json scalar fails with EINVAL");

    /* Using the lower level flux_msg_set_payload with FLUX_MSGFLAG_JSON
     * we can sneak in a malformed JSON payload and test decoding.
     */
    errno = 0;
    ok (flux_msg_set_payload (msg, FLUX_MSGFLAG_JSON, "[1,2,3]", 8) == 0
            && flux_msg_get_json (msg, &s) < 0 && errno == EPROTO,
        "flux_msg_get_json array fails with EPROTO");
    errno = 0;
    ok (flux_msg_set_payload (msg, FLUX_MSGFLAG_JSON, "3.14", 5) == 0
            && flux_msg_get_json (msg, &s) < 0 && errno == EPROTO,
        "flux_msg_get_json scalar fails with EPROTO");

    ok (flux_msg_set_json (msg, json_str) == 0,
       "flux_msg_set_json works");
    ok (flux_msg_get_json (msg, &s) == 0 && s != NULL
        && !strcmp (s, json_str),
       "flux_msg_get_json returns payload intact");

    flux_msg_destroy (msg);
}

void check_payload_json_formatted (void)
{
    flux_msg_t *msg;
    int i;
    const char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    errno = 0;
    ok (flux_msg_get_jsonf (msg, "{}") < 0 && errno == EPROTO,
        "flux_msg_get_jsonf fails with EPROTO with no payload");

    errno = 0;
    ok (flux_msg_set_jsonf (msg, "[i,i,i]", 1,2,3) < 0 && errno == EINVAL,
        "flux_msg_set_jsonf array fails with EINVAL");
    errno = 0;
    ok (flux_msg_set_jsonf (msg, "i", 3.14) < 0 && errno == EINVAL,
       "flux_msg_set_jsonf scalar fails with EINVAL");
    ok (flux_msg_set_jsonf (msg, "{s:i, s:s}", "foo", 42, "bar", "baz") == 0,
       "flux_msg_set_jsonf object works");
    i = 0;
    s = NULL;
    ok (flux_msg_get_jsonf (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_get_jsonf object works");
    ok (i == 42 && s != NULL && !strcmp (s, "baz"),
        "decoded content matches encoded content");

    /* reset payload */
    ok (flux_msg_set_jsonf (msg, "{s:i, s:s}", "foo", 43, "bar", "smurf") == 0,
       "flux_msg_set_jsonf can replace JSON object payload");
    i = 0;
    s = NULL;
    ok (flux_msg_get_jsonf (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_get_jsonf object works");
    ok (i == 43 && s != NULL && !strcmp (s, "smurf"),
        "decoded content matches new encoded content");

    i = 0;
    s = NULL;
    ok (flux_msg_get_jsonf (msg, "{s:s, s:i}", "bar", &s, "foo", &i) == 0,
       "flux_msg_get_jsonf object works out of order");
    ok (i == 43 && s != NULL && !strcmp (s, "smurf"),
        "decoded content matches new encoded content");

    errno = 0;
    ok (flux_msg_get_jsonf (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_get_jsonf fails with EINVAL with NULL format");

    errno = 0;
    ok (flux_msg_get_jsonf (msg, "") < 0 && errno == EINVAL,
        "flux_msg_get_jsonf fails with EINVAL with \"\" format");

    errno = 0;
    ok (flux_msg_get_jsonf (msg, "{s:s}", "nope", &s) < 0 && errno == EPROTO,
        "flux_msg_get_jsonf fails with EPROTO with nonexistent key");

    flux_msg_destroy (msg);
}

/* flux_msg_get_payload, flux_msg_set_payload
 *  on message with and without routes, with and without topic string
 */
void check_payload (void)
{
    flux_msg_t *msg;
    void *pay[1024], *buf;
    int plen = sizeof (pay), len;
    int flags;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload fails with EPROTO on msg w/o payload");
    errno = 0;
    ok (flux_msg_set_payload (msg, 0, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works with no payload");
    errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload still fails");

    errno = 0;
    memset (pay, 42, plen);
    ok (flux_msg_set_payload (msg, 0, pay, plen) == 0
        && flux_msg_frames (msg) == 2,
       "flux_msg_set_payload works");

    len = 0; buf = NULL; flags =0; errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && flux_msg_frames (msg) == 3,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; flags = 0; errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works with topic");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");
    ok (flux_msg_set_topic (msg, NULL) == 0 && flux_msg_frames (msg) == 2,
       "flux_msg_set_topic NULL works");

    ok (flux_msg_enable_route (msg) == 0 && flux_msg_frames (msg) == 3,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (msg, "id1") == 0 && flux_msg_frames (msg) == 4,
        "flux_msg_push_route works");

    len = 0; buf = NULL; flags =0; errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload still works, with routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && flux_msg_frames (msg) == 5,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; flags = 0; errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works, with topic and routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    errno = 0;
    ok (flux_msg_set_payload (msg, 0, buf, len - 1) < 0 && errno == EINVAL,
        "flux_msg_set_payload detects reuse of payload fragment and fails with EINVAL");

    ok (flux_msg_set_payload (msg, 0, buf, len) == 0,
        "flux_msg_set_payload detects payload echo and works");
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    errno = 0;
    ok (flux_msg_set_payload (msg, 0, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works");
    errno = 0;
    ok (flux_msg_get_payload (msg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload now fails with EPROTO");

    flux_msg_destroy (msg);
}

/* flux_msg_set_type, flux_msg_get_type
 * flux_msg_set_nodeid, flux_msg_get_nodeid
 * flux_msg_set_errnum, flux_msg_get_errnum
 */
void check_proto (void)
{
    flux_msg_t *msg;
    uint32_t nodeid;
    int errnum;
    int type;
    int flags;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");

    ok (flux_msg_set_type (msg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_get_nodeid (msg, &nodeid, &flags) == 0
        && nodeid == FLUX_NODEID_ANY
        && flags == 0,
        "flux_msg_get_nodeid works on request and default is sane");

    nodeid = 42;
    ok (flux_msg_set_nodeid (msg, nodeid, 0) == 0,
        "flux_msg_set_nodeid works on request");
    nodeid = 0;
    ok (flux_msg_get_nodeid (msg, &nodeid, &flags) == 0
        && nodeid == 42
        && flags == 0,
        "flux_msg_get_nodeid works and returns what we set");

    errno = 0;
    ok (flux_msg_set_errnum (msg, 42) < 0 && errno == EINVAL,
        "flux_msg_set_errnum on non-response fails with errno == EINVAL");
    ok (flux_msg_set_type (msg, FLUX_MSGTYPE_RESPONSE) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_set_errnum (msg, 43) == 0,
        "flux_msg_set_errnum works on response");
    errno = 0;
    ok (flux_msg_set_nodeid (msg, 0, 0) < 0 && errno == EINVAL,
        "flux_msg_set_nodeid on non-request fails with errno == EINVAL");
    errnum = 0;
    ok (flux_msg_get_errnum (msg, &errnum) == 0 && errnum == 43,
        "flux_msg_get_errnum works and returns what we set");

    ok (flux_msg_set_type (msg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    errno = 0;
    ok (flux_msg_set_nodeid (msg, FLUX_NODEID_ANY, FLUX_MSGFLAG_UPSTREAM) < 0
        && errno == EINVAL,
        "flux_msg_set_nodeid ANY + FLUX_MSGFLAG_UPSTREAM fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_nodeid (msg, FLUX_NODEID_UPSTREAM, 0) < 0
        && errno == EINVAL,
        "flux_msg_set_nodeid FLUX_NODEID_UPSTREAM fails with EINVAL");

    ok (flux_msg_set_nodeid (msg, 42, FLUX_MSGFLAG_UPSTREAM) == 0
        && flux_msg_get_nodeid (msg, &nodeid, &flags) == 0
        && nodeid == 42 && flags == FLUX_MSGFLAG_UPSTREAM,
        "flux_msg_set_nodeid with nodeid + FLUX_MSGFLAG_UPSTREAM works");

    flux_msg_destroy (msg);
}

void check_matchtag (void)
{
    flux_msg_t *msg;
    uint32_t t;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_matchtag (msg, &t) == 0 && t == FLUX_MATCHTAG_NONE,
        "flux_msg_get_matchtag returns FLUX_MATCHTAG_NONE  when uninitialized");
    ok (flux_msg_set_matchtag (msg, 42) == 0,
        "flux_msg_set_matchtag works");
    ok (flux_msg_get_matchtag (msg, &t) == 0,
        "flux_msg_get_matchtag works");
    ok (t == 42,
        "flux_msg_get_matchtag returns set value");
    ok (flux_msg_cmp_matchtag (msg, 42) && !flux_msg_cmp_matchtag (msg, 0),
        "flux_msg_cmp_matchtag works");

    ok (flux_msg_set_matchtag (msg, (1<<FLUX_MATCHTAG_GROUP_SHIFT) | 55) == 0,
        "flux_msg_set_matchtag (group part nonzero) works ");
    ok (flux_msg_cmp_matchtag (msg, (1<<FLUX_MATCHTAG_GROUP_SHIFT | 69))
        && !flux_msg_cmp_matchtag (msg, (3<<FLUX_MATCHTAG_GROUP_SHIFT)),
        "flux_msg_cmp_matchtag compares only group part if nonzero");
    flux_msg_destroy (msg);
}

void check_cmp (void)
{
    struct flux_match match = FLUX_MATCH_ANY;
    flux_msg_t *msg;
    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");

    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp all-match works");

    match.typemask = FLUX_MSGTYPE_RESPONSE;
    ok (!flux_msg_cmp (msg, match),
        "flux_msg_cmp with request type not in mask works");

    match.typemask |= FLUX_MSGTYPE_REQUEST;
    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp with request type in mask works");

    ok (flux_msg_set_topic (msg, "hello.foo") == 0,
        "flux_msg_set_topic works");
    match.topic_glob = "hello.foobar";
    ok (!flux_msg_cmp (msg, match),
        "flux_msg_cmp with unmatched topic works");

    match.topic_glob = "hello.foo";
    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp with exact topic works");

    match.topic_glob = "hello.*";
    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp with globbed topic works");
    flux_msg_destroy (msg);
}

void check_encode (void)
{
    flux_msg_t *msg, *msg2;
    void *buf;
    size_t size;
    const char *topic;
    int type;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "flux_msg_set_topic works");
    size = flux_msg_encode_size (msg);
    ok (size > 0,
        "flux_msg_encode_size works");
    buf = malloc (size);
    assert (buf != NULL);
    ok (flux_msg_encode (msg, buf, size) == 0,
        "flux_msg_encode works");
    ok ((msg2 = flux_msg_decode (buf, size)) != NULL,
        "flux_msg_decode works");
    free (buf);
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "decoded expected message type");
    ok (flux_msg_get_topic (msg2, &topic) == 0 && !strcmp (topic, "foo.bar"),
        "decoded expected topic string");
    ok (flux_msg_has_payload (msg2) == false,
        "decoded expected (lack of) payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
}

/* Send a small message over a blocking pipe.
 * We assume that there's enough buffer to do this in one go.
 */
void check_sendfd (void)
{
    int pfd[2];
    flux_msg_t *msg, *msg2;
    const char *topic;
    int type;

    ok (pipe2 (pfd, O_CLOEXEC) == 0,
        "got blocking pipe");
    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "flux_msg_set_topic works");
    ok (flux_msg_sendfd (pfd[1], msg, NULL) == 0,
        "flux_msg_sendfd works");
    ok ((msg2 = flux_msg_recvfd (pfd[0], NULL)) != NULL,
        "flux_msg_recvfd works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "decoded expected message type");
    ok (flux_msg_get_topic (msg2, &topic) == 0 && !strcmp (topic, "foo.bar"),
        "decoded expected topic string");
    ok (flux_msg_has_payload (msg2) == false,
        "decoded expected (lack of) payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
    close (pfd[1]);
    close (pfd[0]);
}

void check_sendzsock (void)
{
    zctx_t *zctx;
    void *zsock[2] = { NULL, NULL };
    flux_msg_t *msg, *msg2;
    const char *topic;
    int type;
    const char *uri = "inproc://test";

    ok ((zctx = zctx_new ()) && (zsock[0] = zsocket_new (zctx, ZMQ_PAIR))
                             && (zsock[1] = zsocket_new (zctx, ZMQ_PAIR))
                             && zsocket_bind (zsock[0], "%s", uri) == 0
                             && zsocket_connect (zsock[1], "%s", uri) == 0,
        "got inproc socket pair");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
            && flux_msg_set_topic (msg, "foo.bar") == 0,
        "created test message");

    ok (flux_msg_sendzsock (zsock[1], msg) == 0,
        "flux_msg_sendzsock works");
    ok ((msg2 = flux_msg_recvzsock (zsock[0])) != NULL,
        "flux_msg_recvzsock works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && !strcmp (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "decoded message looks like what was sent");
    flux_msg_destroy (msg2);

    /* Send it again.
     */
    ok (flux_msg_sendzsock (zsock[1], msg) == 0,
        "try2: flux_msg_sendzsock works");
    ok ((msg2 = flux_msg_recvzsock (zsock[0])) != NULL,
        "try2: flux_msg_recvzsock works");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg2, &topic) == 0
            && !strcmp (topic, "foo.bar")
            && flux_msg_has_payload (msg2) == false,
        "try2: decoded message looks like what was sent");
    flux_msg_destroy (msg2);

    flux_msg_destroy (msg);
    zctx_destroy (&zctx);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_proto ();
    check_routes ();
    check_topic ();
    check_payload ();
    check_payload_json ();
    check_payload_json_formatted ();
    check_matchtag ();

    check_cmp ();

    check_encode ();
    check_sendfd ();
    check_sendzsock ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

