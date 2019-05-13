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
#include <czmq.h>
#include <errno.h>
#include <stdio.h>
#include <jansson.h>

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
    json_t *o;
    int i;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");

    s = (char *)msg;
    ok (flux_msg_get_string (msg, &s) == 0 && s == NULL,
       "flux_msg_get_string returns success with no payload");

    /* RFC 3 - json payload must be an object
     * Encoding should return EINVAL.
     */
    errno = 0;
    ok (flux_msg_pack (msg, "[1,2,3]") < 0 && errno == EINVAL,
       "flux_msg_pack array fails with EINVAL");
    errno = 0;
    ok (flux_msg_pack (msg, "3.14") < 0 && errno == EINVAL,
       "flux_msg_pack scalar fails with EINVAL");

    /* Sneak in a malformed JSON payloads and test decoding.
     * 1) array
     */
    if (flux_msg_set_string (msg, "[1,2,3]") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack array fails with EPROTO");
    /* 2) bare value
     */
    if (flux_msg_set_string (msg, "3.14") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack scalar fails with EPROTO");
    /* 3) malformed object (no trailing })
     */
    if (flux_msg_set_string (msg, "{\"a\":42") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack malformed object fails with EPROTO");

    ok (flux_msg_pack (msg, "{s:i}", "foo", 42) == 0,
       "flux_msg_pack works");
    i = 0;
    ok (flux_msg_unpack (msg, "{s:i}", "foo", &i) == 0 && i == 42,
       "flux_msg_unpack returns payload intact");

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
    ok (flux_msg_unpack (msg, "{}") < 0 && errno == EPROTO,
        "flux_msg_unpack fails with EPROTO with no payload");

    errno = 0;
    ok (flux_msg_pack (msg, "[i,i,i]", 1,2,3) < 0 && errno == EINVAL,
        "flux_msg_pack array fails with EINVAL");
    errno = 0;
    ok (flux_msg_pack (msg, "i", 3.14) < 0 && errno == EINVAL,
       "flux_msg_pack scalar fails with EINVAL");
    ok (flux_msg_pack (msg, "{s:i, s:s}", "foo", 42, "bar", "baz") == 0,
       "flux_msg_pack object works");
    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_unpack object works");
    ok (i == 42 && s != NULL && !strcmp (s, "baz"),
        "decoded content matches encoded content");

    /* reset payload */
    ok (flux_msg_pack (msg, "{s:i, s:s}", "foo", 43, "bar", "smurf") == 0,
       "flux_msg_pack can replace JSON object payload");
    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_unpack object works");
    ok (i == 43 && s != NULL && !strcmp (s, "smurf"),
        "decoded content matches new encoded content");

    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:s, s:i}", "bar", &s, "foo", &i) == 0,
       "flux_msg_unpack object works out of order");
    ok (i == 43 && s != NULL && !strcmp (s, "smurf"),
        "decoded content matches new encoded content");

    errno = 0;
    ok (flux_msg_unpack (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_unpack fails with EINVAL with NULL format");

    errno = 0;
    ok (flux_msg_unpack (msg, "") < 0 && errno == EINVAL,
        "flux_msg_unpack fails with EINVAL with \"\" format");

    errno = 0;
    ok (flux_msg_unpack (msg, "{s:s}", "nope", &s) < 0 && errno == EPROTO,
        "flux_msg_unpack fails with EPROTO with nonexistent key");

    flux_msg_destroy (msg);
}

/* flux_msg_get_payload, flux_msg_set_payload
 *  on message with and without routes, with and without topic string
 */
void check_payload (void)
{
    flux_msg_t *msg;
    const void *buf;
    void *pay[1024];
    int plen = sizeof (pay), len;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload fails with EPROTO on msg w/o payload");
    errno = 0;
    ok (flux_msg_set_payload (msg, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works with no payload");
    errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload still fails");

    errno = 0;
    memset (pay, 42, plen);
    ok (flux_msg_set_payload (msg, pay, plen) == 0
        && flux_msg_frames (msg) == 2,
       "flux_msg_set_payload works");

    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && flux_msg_frames (msg) == 3,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload works with topic");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");
    ok (flux_msg_set_topic (msg, NULL) == 0 && flux_msg_frames (msg) == 2,
       "flux_msg_set_topic NULL works");

    ok (flux_msg_enable_route (msg) == 0 && flux_msg_frames (msg) == 3,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (msg, "id1") == 0 && flux_msg_frames (msg) == 4,
        "flux_msg_push_route works");

    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload still works, with routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && flux_msg_frames (msg) == 5,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload works, with topic and routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    errno = 0;
    ok (flux_msg_set_payload (msg, buf, len - 1) < 0 && errno == EINVAL,
        "flux_msg_set_payload detects reuse of payload fragment and fails with EINVAL");

    ok (flux_msg_set_payload (msg, buf, len) == 0,
        "flux_msg_set_payload detects payload echo and works");
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    errno = 0;
    ok (flux_msg_set_payload (msg, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works");
    errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) < 0 && errno == EPROTO,
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

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");

    ok (flux_msg_set_type (msg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (msg, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_get_nodeid (msg, &nodeid) == 0
        && nodeid == FLUX_NODEID_ANY,
        "flux_msg_get_nodeid works on request and default is sane");

    nodeid = 42;
    ok (flux_msg_set_nodeid (msg, nodeid) == 0,
        "flux_msg_set_nodeid works on request");
    nodeid = 0;
    ok (flux_msg_get_nodeid (msg, &nodeid) == 0
        && nodeid == 42,
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
    ok (flux_msg_set_nodeid (msg, 0) < 0 && errno == EINVAL,
        "flux_msg_set_nodeid on non-request fails with errno == EINVAL");
    errnum = 0;
    ok (flux_msg_get_errnum (msg, &errnum) == 0 && errnum == 43,
        "flux_msg_get_errnum works and returns what we set");

    ok (flux_msg_set_type (msg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");

    errno = 0;
    ok (flux_msg_set_nodeid (msg, FLUX_NODEID_UPSTREAM) < 0
        && errno == EINVAL,
        "flux_msg_set_nodeid FLUX_NODEID_UPSTREAM fails with EINVAL");

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

void check_security (void)
{
    flux_msg_t *msg;
    uint32_t userid, rolemask;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_userid (msg, &userid) == 0
        && userid == FLUX_USERID_UNKNOWN,
        "message created with userid=FLUX_USERID_UNKNOWN");
    ok (flux_msg_get_rolemask (msg, &rolemask) == 0
        && rolemask == FLUX_ROLE_NONE,
        "message created with rolemask=FLUX_ROLE_NONE");
    ok (flux_msg_set_userid (msg, 4242) == 0
        && flux_msg_get_userid (msg, &userid) == 0
        && userid == 4242,
        "flux_msg_set_userid 4242 works");
    ok (flux_msg_set_rolemask (msg, FLUX_ROLE_ALL) == 0
        && flux_msg_get_rolemask (msg, &rolemask) == 0
        && rolemask == FLUX_ROLE_ALL,
        "flux_msg_set_rolemask FLUX_ROLE_ALL works");
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
    zsock_t *zsock[2] = { NULL, NULL };
    flux_msg_t *msg, *msg2;
    const char *topic;
    int type;
    const char *uri = "inproc://test";

    ok ((zsock[0] = zsock_new_pair (NULL)) != NULL
                    && zsock_bind (zsock[0], "%s", uri) == 0
                    && (zsock[1] = zsock_new_pair (uri)) != NULL,
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

    zsock_destroy (&zsock[0]);
    zsock_destroy (&zsock[1]);
}

void *myfree_arg = NULL;
void myfree (void *arg)
{
    myfree_arg = arg;
}

void check_aux (void)
{
    flux_msg_t *msg;
    char *test_data = "Hello";

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    errno = 0;
    ok (flux_msg_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "flux_msg_aux_set msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_msg_aux_get msg=NULL fails with EINVAL");
    ok (flux_msg_aux_set (msg, "test", test_data, myfree) == 0,
        "hang aux data member on message with destructor");
    ok (flux_msg_aux_get (msg, "incorrect") == NULL,
        "flux_msg_aux_get for unknown key returns NULL");
    ok (flux_msg_aux_get (msg, "test") == test_data,
        "flux_msg_aux_get aux data memeber key returns orig pointer");
    flux_msg_destroy (msg);
    ok (myfree_arg == test_data,
        "destroyed message and aux destructor was called");
}

void check_copy (void)
{
    flux_msg_t *msg, *cpy;
    int type;
    const char *topic;
    int cpylen;
    const char buf[] = "xxxxxxxxxxxxxxxxxx";
    const void *cpybuf;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)) != NULL,
        "created no-payload keepalive");
    ok ((cpy = flux_msg_copy (msg, true)) != NULL,
        "flux_msg_copy works");
    flux_msg_destroy (msg);
    type = -1;
    ok (flux_msg_get_type (cpy, &type) == 0 && type == FLUX_MSGTYPE_KEEPALIVE
             && !flux_msg_has_payload (cpy)
             && flux_msg_get_route_count (cpy) < 0
             && flux_msg_get_topic (cpy, &topic) < 0,
        "copy is keepalive: no routes, topic, or payload");
    flux_msg_destroy (cpy);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created request");
    ok (flux_msg_enable_route (msg) == 0,
        "added route delim");
    ok (flux_msg_set_topic (msg, "foo") == 0,
        "set topic string");
    ok (flux_msg_set_payload (msg, buf, sizeof (buf)) == 0,
        "added payload");
    ok ((cpy = flux_msg_copy (msg, true)) != NULL,
        "flux_msg_copy works");
    type = -1;
    ok (flux_msg_get_type (cpy, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
             && flux_msg_has_payload (cpy)
             && flux_msg_get_payload (cpy, &cpybuf, &cpylen) == 0
             && cpylen == sizeof (buf) && memcmp (cpybuf, buf, cpylen) == 0
             && flux_msg_get_route_count (cpy) == 0
             && flux_msg_get_topic (cpy, &topic) == 0 && !strcmp (topic,"foo"),
        "copy is request: w/route delim, topic, and payload");
    flux_msg_destroy (cpy);

    ok ((cpy = flux_msg_copy (msg, false)) != NULL,
        "flux_msg_copy works (payload=false)");
    type = -1;
    ok (flux_msg_get_type (cpy, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
             && !flux_msg_has_payload (cpy)
             && flux_msg_get_route_count (cpy) == 0
             && flux_msg_get_topic (cpy, &topic) == 0 && !strcmp (topic,"foo"),
        "copy is request: w/route delim, topic, and no payload");
    flux_msg_destroy (cpy);
    flux_msg_destroy (msg);
}

void check_print (void)
{
    flux_msg_t *msg;
    char buf[] = "xxxxxxxx";
    FILE *f = fopen ("/dev/null", "w");
    if (!f)
        BAIL_OUT ("cannot open /dev/null for writing");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)) != NULL,
        "created test message");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on keepalive");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_EVENT)) != NULL,
        "created test message");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "set topic string");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on event with topic");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "set topic string");
    ok (flux_msg_enable_route (msg) == 0,
        "enabled routing");
    ok (flux_msg_push_route (msg, "id1") == 0,
        "added one route");
    ok (flux_msg_set_payload (msg, buf, strlen (buf)) == 0,
        "added payload");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on fully loaded request");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "created test message");
    ok (flux_msg_enable_route (msg) == 0,
        "enabled routing");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on response with empty route stack");
    flux_msg_destroy (msg);

    fclose (f);
}

void check_params (void)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        BAIL_OUT ("flux_msg_create failed");
    errno = 0;
    ok (flux_msg_set_payload (NULL, NULL, 0) < 0 && errno == EINVAL,
        "flux_msg_set_payload msg=NULL fails with EINVAL");

    flux_msg_destroy (msg);
}

void check_flags (void)
{
    flux_msg_t *msg;
    uint8_t flags;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    ok (flux_msg_get_flags (msg, &flags) == 0,
        "flux_msg_get_flags works");
    ok (flags == 0,
        "flags are initially zero");

    /* FLUX_MSGFLAG_PRIVATE */
    ok (flux_msg_is_private (msg) == false,
        "flux_msg_is_private = false");
    ok (flux_msg_set_private (msg) == 0,
        "flux_msg_set_private_works");
    ok (flux_msg_is_private (msg) == true,
        "flux_msg_is_private = true");

    /* FLUX_MSGFLAG_STREAMING */
    ok (flux_msg_is_streaming (msg) == false,
        "flux_msg_is_streaming = false");
    ok (flux_msg_set_streaming (msg) == 0,
        "flux_msg_set_streaming_works");
    ok (flux_msg_is_streaming (msg) == true,
        "flux_msg_is_streaming = true");

    ok (flux_msg_set_topic (msg, "foo") == 0
        && flux_msg_get_flags (msg, &flags) == 0
        && (flags & FLUX_MSGFLAG_TOPIC),
        "flux_msg_set_topic sets FLUX_MSGFLAG_TOPIC");

    ok (flux_msg_set_payload (msg, "foo", 3) == 0
        && flux_msg_get_flags (msg, &flags) == 0
        && (flags & FLUX_MSGFLAG_PAYLOAD),
        "flux_msg_set_payload sets FLUX_MSGFLAG_PAYLOAD");

    ok (flux_msg_enable_route (msg) == 0
        && flux_msg_get_flags (msg, &flags) == 0
        && (flags & FLUX_MSGFLAG_ROUTE),
        "flux_msg_enable_route sets FLUX_MSGFLAG_ROUTE");

    flux_msg_destroy (msg);

    /* invalid params checks */

    errno = 0;
    ok (flux_msg_get_flags (NULL, &flags) < 0 && errno == EINVAL,
        "flux_msg_get_flags msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_get_flags (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_get_flags flags=NULL fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_flags (NULL, 0) < 0 && errno == EINVAL,
        "flux_msg_set_flags msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_set_flags (msg, 0xff) < 0 && errno == EINVAL,
        "flux_msg_set_flags flags=(invalid) fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_private (NULL) < 0 && errno == EINVAL,
        "flux_msg_set_private msg=NULL fails with EINVAL");
    ok (flux_msg_is_private (NULL) == true,
        "flux_msg_is_private msg=NULL returns true");

    errno = 0;
    ok (flux_msg_set_streaming (NULL) < 0 && errno == EINVAL,
        "flux_msg_set_streaming msg=NULL fails with EINVAL");
    ok (flux_msg_is_streaming (NULL) == true,
        "flux_msg_is_streaming msg=NULL returns true");
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
    check_security ();
    check_aux ();
    check_copy ();
    check_flags ();

    check_cmp ();

    check_encode ();
    check_sendfd ();
    check_sendzsock ();

    check_params ();

    //check_print ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

