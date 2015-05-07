#include "src/common/libflux/message.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/shortjson.h"

/* flux_msg_get_route_first, flux_msg_get_route_last, _get_route_count
 *   on message with variable number of routing frames
 */
void check_routes (void)
{
    zmsg_t *zmsg;
    char *s;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
        && zmsg_size (zmsg) == 1,
        "flux_msg_create works and creates msg with 1 frame");
    errno = 0;
    ok (flux_msg_get_route_count (zmsg) < 0 && errno == EPROTO,
        "flux_msg_get_route_count returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_first (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_first returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_last (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_last returns -1 errno EPROTO on msg w/o delim");
    ok ((flux_msg_pop_route (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_pop_route returns -1 errno EPROTO on msg w/o delim");

    ok (flux_msg_clear_route (zmsg) == 0 && zmsg_size (zmsg) == 1,
        "flux_msg_clear_route works, is no-op on msg w/o delim");
    ok (flux_msg_enable_route (zmsg) == 0 && zmsg_size (zmsg) == 2,
        "flux_msg_enable_route works, adds one frame on msg w/o delim");
    ok ((flux_msg_get_route_count (zmsg) == 0),
        "flux_msg_get_route_count returns 0 on msg w/delim");
    ok (flux_msg_pop_route (zmsg, &s) == 0 && s == NULL,
        "flux_msg_pop_route works and sets id to NULL on msg w/o routes");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s == NULL,
        "flux_msg_get_route_first returns 0, id=NULL on msg w/delim");
    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s == NULL,
        "flux_msg_get_route_last returns 0, id=NULL on msg w/delim");
    ok (flux_msg_push_route (zmsg, "sender") == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (zmsg) == 1),
        "flux_msg_get_route_count returns 1 on msg w/delim+id");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "sender",
        "flux_msg_get_route_last returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_push_route (zmsg, "router") == 0 && zmsg_size (zmsg) == 4,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (zmsg) == 2),
        "flux_msg_get_route_count returns 2 on msg w/delim+id1+id2");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id1 on msg w/delim+id1+id2");
    free (s);

    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "router",
        "flux_msg_get_route_last returns id2 on message with delim+id1+id2");
    free (s);

    s = NULL;
    ok (flux_msg_pop_route (zmsg, &s) == 0 && s != NULL,
        "flux_msg_pop_route works on msg w/routes");
    like (s, "router",
        "flux_msg_pop_routet returns id2 on message with delim+id1+id2");
    free (s);

    ok (flux_msg_clear_route (zmsg) == 0 && zmsg_size (zmsg) == 1,
        "flux_msg_clear_route strips routing frames and delim");
    zmsg_destroy (&zmsg);
}

/* flux_msg_get_topic, flux_msg_set_topic, flux_msg_streq_topic,
 *  flux_msg_strneq_topic on message with and without routes
 */
void check_topic (void)
{
    zmsg_t *zmsg;
    char *s;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "zmsg_create works");
    errno = 0;
    ok (flux_msg_get_topic (zmsg, &s) < 0 && errno == EPROTO,
       "flux_msg_get_topic fails with EPROTO on msg w/o topic");
    ok (flux_msg_set_topic (zmsg, "blorg") == 0,
       "flux_msg_set_topic works");
    ok (flux_msg_get_topic (zmsg, &s) == 0,
       "flux_msg_get_topic works on msg w/topic");
    like (s, "blorg",
       "and we got back the topic string we set");
    free (s);

    ok (flux_msg_enable_route (zmsg) == 0,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (zmsg, "id1") == 0,
        "flux_msg_push_route works");
    ok (flux_msg_get_topic (zmsg, &s) == 0,
       "flux_msg_get_topic still works, with routes");
    like (s, "blorg",
       "and we got back the topic string we set");
    free (s);

    ok (   !flux_msg_streq_topic (zmsg, "")
        && !flux_msg_streq_topic (zmsg, "bl")
        &&  flux_msg_streq_topic (zmsg, "blorg")
        && !flux_msg_streq_topic (zmsg, "blorgnax"),
        "flux_msg_streq_topic works");
    ok (    flux_msg_strneq_topic (zmsg, "", 0)
        &&  flux_msg_strneq_topic (zmsg, "bl", 2)
        &&  flux_msg_strneq_topic(zmsg, "blorg", 5)
        && !flux_msg_strneq_topic(zmsg, "blorgnax", 8),
        "flux_msg_strneq_topic works");

    zmsg_destroy (&zmsg);
}

/* flux_msg_get_payload, flux_msg_set_payload
 *  on message with and without routes, with and without topic string
 */
void check_payload (void)
{
    zmsg_t *zmsg;
    void *pay[1024], *buf;
    int plen = sizeof (pay), len;
    int flags;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "zmsg_create works");
    errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload fails with EPROTO on msg w/o topic");
    errno = 0;
    ok (flux_msg_set_payload (zmsg, 0, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works with no payload");
    errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload still fails");

    errno = 0;
    memset (pay, 42, plen);
    ok (flux_msg_set_payload (zmsg, 0, pay, plen) == 0
        && zmsg_size (zmsg) == 2,
       "flux_msg_set_payload works");

    len = 0; buf = NULL; flags =0; errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (zmsg, "blorg") == 0 && zmsg_size (zmsg) == 3,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; flags = 0; errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works with topic");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");
    ok (flux_msg_set_topic (zmsg, NULL) == 0 && zmsg_size (zmsg) == 2,
       "flux_msg_set_topic NULL works");

    ok (flux_msg_enable_route (zmsg) == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (zmsg, "id1") == 0 && zmsg_size (zmsg) == 4,
        "flux_msg_push_route works");

    len = 0; buf = NULL; flags =0; errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload still works, with routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (zmsg, "blorg") == 0 && zmsg_size (zmsg) == 5,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; flags = 0; errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) == 0
        && buf && len == plen && flags == 0 && errno == 0,
       "flux_msg_get_payload works, with topic and routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    errno = 0;
    ok (flux_msg_set_payload (zmsg, 0, buf, len) < 0 && errno == EINVAL,
        "flux_msg_set_payload detects reuse of payload and fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_payload (zmsg, 0, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works");
    errno = 0;
    ok (flux_msg_get_payload (zmsg, &flags, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload now fails with EPROTO");

    zmsg_destroy (&zmsg);
}

/* flux_msg_set_type, flux_msg_get_type
 * flux_msg_set_nodeid, flux_msg_get_nodeid
 * flux_msg_set_errnum, flux_msg_get_errnum
 */
void check_proto (void)
{
    zmsg_t *zmsg;
    uint32_t nodeid;
    int errnum;
    int type;
    int flags;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");

    ok (flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_get_nodeid (zmsg, &nodeid, &flags) == 0
        && nodeid == FLUX_NODEID_ANY
        && flags == 0,
        "flux_msg_get_nodeid works on request and default is sane");

    nodeid = 42;
    ok (flux_msg_set_nodeid (zmsg, nodeid, 0) == 0,
        "flux_msg_set_nodeid works on request");
    nodeid = 0;
    ok (flux_msg_get_nodeid (zmsg, &nodeid, &flags) == 0
        && nodeid == 42
        && flags == 0,
        "flux_msg_get_nodeid works and returns what we set");

    errno = 0;
    ok (flux_msg_set_errnum (zmsg, 42) < 0 && errno == EINVAL,
        "flux_msg_set_errnum on non-response fails with errno == EINVAL");
    ok (flux_msg_set_type (zmsg, FLUX_MSGTYPE_RESPONSE) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_set_errnum (zmsg, 43) == 0,
        "flux_msg_set_errnum works on response");
    errno = 0;
    ok (flux_msg_set_nodeid (zmsg, 0, 0) < 0 && errno == EINVAL,
        "flux_msg_set_nodeid on non-request fails with errno == EINVAL");
    errnum = 0;
    ok (flux_msg_get_errnum (zmsg, &errnum) == 0 && errnum == 43,
        "flux_msg_get_errnum works and returns what we set");

    ok (flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    errno = 0;
    ok (flux_msg_set_nodeid (zmsg, FLUX_NODEID_ANY, FLUX_MSGFLAG_UPSTREAM) < 0
        && errno == EINVAL,
        "flux_msg_set_nodeid ANY + FLUX_MSGFLAG_UPSTREAM fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_nodeid (zmsg, FLUX_NODEID_UPSTREAM, 0) < 0
        && errno == EINVAL,
        "flux_msg_set_nodeid FLUX_NODEID_UPSTREAM fails with EINVAL");

    ok (flux_msg_set_nodeid (zmsg, 42, FLUX_MSGFLAG_UPSTREAM) == 0
        && flux_msg_get_nodeid (zmsg, &nodeid, &flags) == 0
        && nodeid == 42 && flags == FLUX_MSGFLAG_UPSTREAM,
        "flux_msg_set_nodeid with nodeid + FLUX_MSGFLAG_UPSTREAM works");

    zmsg_destroy (&zmsg);
}

void check_matchtag (void)
{
    zmsg_t *zmsg;
    uint32_t t;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_matchtag (zmsg, &t) == 0 && t == FLUX_MATCHTAG_NONE,
        "flux_msg_get_matchtag returns FLUX_MATCHTAG_NONE  when uninitialized");
    ok (flux_msg_set_matchtag (zmsg, 42) == 0,
        "flux_msg_set_matchtag works");
    ok (flux_msg_get_matchtag (zmsg, &t) == 0,
        "flux_msg_get_matchtag works");
    ok (t == 42,
        "flux_msg_get_matchtag returns set value");
    ok (flux_msg_cmp_matchtag (zmsg, 42) && !flux_msg_cmp_matchtag (zmsg, 0),
        "flux_msg_cmp_matchtag works");
    zmsg_destroy (&zmsg);
}

void check_cmp (void)
{
    flux_match_t match = {
        .typemask = 0,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 0,
        .topic_glob = NULL,
    };
    zmsg_t *zmsg;
    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");

    ok (flux_msg_cmp (zmsg, match),
        "flux_msg_cmp all-match works");

    match.typemask = FLUX_MSGTYPE_RESPONSE;
    ok (!flux_msg_cmp (zmsg, match),
        "flux_msg_cmp with request type not in mask works");

    match.typemask |= FLUX_MSGTYPE_REQUEST;
    ok (flux_msg_cmp (zmsg, match),
        "flux_msg_cmp with request type in mask works");

    ok (flux_msg_set_topic (zmsg, "hello.foo") == 0,
        "flux_msg_set_topic works");
    match.topic_glob = "hello.foobar";
    ok (!flux_msg_cmp (zmsg, match),
        "flux_msg_cmp with unmatched topic works");

    match.topic_glob = "hello.foo";
    ok (flux_msg_cmp (zmsg, match),
        "flux_msg_cmp with exact topic works");

    match.topic_glob = "hello.*";
    ok (flux_msg_cmp (zmsg, match),
        "flux_msg_cmp with globbed topic works");
    zmsg_destroy (&zmsg);
}

int main (int argc, char *argv[])
{
    plan (90);

    lives_ok ({zmsg_test (false);}, // 1
        "zmsg_test doesn't assert");

    check_proto ();                 // 17
    check_routes ();                // 26
    check_topic ();                 // 11
    check_payload ();               // 21
    check_matchtag ();              // 6

    check_cmp ();                   // 8

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

