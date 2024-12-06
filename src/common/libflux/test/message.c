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
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <jansson.h>
#include <string.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#include "message_private.h"

static bool verbose = false;

void check_cornercase (void)
{
    flux_msg_t *msg;
    flux_msg_t *req, *rsp, *evt;
    struct flux_msg_cred cred;
    uint32_t seq, nodeid;
    uint8_t encodebuf[64];
    size_t encodesize = 64;
    int type, errnum, status;
    uint32_t tag;
    const char *topic;
    const void *payload;
    size_t payload_size;

    errno = 0;
    ok (flux_msg_create (0xFFFF) == NULL && errno == EINVAL,
        "flux_msg_create fails with EINVAL on invalid type");

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(req = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(rsp = flux_msg_create (FLUX_MSGTYPE_RESPONSE)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(evt = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        BAIL_OUT ("flux_msg_create failed");

    lives_ok ({flux_msg_destroy (NULL);},
        "flux_msg_destroy msg=NULL doesnt crash");

    errno = 0;
    ok (flux_msg_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "flux_msg_aux_set msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_msg_aux_get msg=NULL fails with EINVAL");

    errno = 0;
    ok (flux_msg_copy (NULL, true) == NULL && errno == EINVAL,
        "flux_msg_copy msg=NULL fails with EINVAL");

    errno = 0;
    ok (flux_msg_incref (NULL) == NULL && errno == EINVAL,
        "flux_msg_incref msg=NULL fails with EINVAL");
    lives_ok ({flux_msg_decref (NULL);},
        "flux_msg_decref msg=NULL doesnt crash");

    errno = 0;
    ok (flux_msg_encode_size (NULL) < 0 && errno == EINVAL,
        "flux_msg_encode_size fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_encode (NULL, encodebuf, encodesize) < 0 && errno == EINVAL,
        "flux_msg_encode fails on EINVAL with msg=NULL");
    errno = 0;
    ok (msg_frames (NULL) < 0 && errno == EINVAL,
        "msg_frames returns -1 errno EINVAL on msg = NULL");

    errno = 0;
    ok (flux_msg_set_type (NULL, 0) < 0 && errno == EINVAL,
        "flux_msg_set_type fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_type (NULL, &type) < 0 && errno == EINVAL,
        "flux_msg_get_type fails with EINVAL on msg = NULL");
    lives_ok ({flux_msg_get_type (msg, NULL);},
        "flux_msg_get_type doesn't segfault with NULL type arg");

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
    errno = 0;
    ok (flux_msg_set_noresponse (NULL) < 0 && errno == EINVAL,
        "flux_msg_set_noresponse msg=NULL fails with EINVAL");
    ok (flux_msg_is_noresponse (NULL) == true,
        "flux_msg_is_noresponse msg=NULL returns true");

    errno = 0;
    ok (flux_msg_set_topic (NULL, "foobar") < 0 && errno == EINVAL,
       "flux_msg_set_topic fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_topic (msg, NULL) < 0 && errno == EINVAL,
       "flux_msg_get_topic fails with EINVAL on in-and-out param = NULL");
    errno = 0;
    ok (flux_msg_get_topic (msg, &topic) < 0 && errno == EPROTO,
       "flux_msg_get_topic fails with EPROTO on msg w/o topic");

    errno = 0;
    ok (flux_msg_set_payload (NULL, NULL, 0) < 0 && errno == EINVAL,
        "flux_msg_set_payload msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_get_payload (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_msg_get_payload msg=NULL fails with EINVAL");
    lives_ok ({flux_msg_get_payload (msg, NULL, NULL);},
       "flux_msg_get_payload does not segfault on in-and-out params = NULL");
    errno = 0;
    ok (flux_msg_get_payload (msg, &payload, &payload_size) < 0
        && errno == EPROTO,
       "flux_msg_get_payload fails with EPROTO on msg w/o payload");
    ok (flux_msg_has_payload (NULL) == false,
        "flux_msg_has_payload returns false on msg = NULL");

    errno = 0;
    ok (flux_msg_set_flag (NULL, FLUX_MSGFLAG_STREAMING) < 0
        && errno == EINVAL,
        "flux_msg_set_flag msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_clear_flag (NULL, FLUX_MSGFLAG_STREAMING) < 0
        && errno == EINVAL,
        "flux_msg_clear_flag msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_set_flag (msg, 0x80000000) < 0 && errno == EINVAL,
        "flux_msg_set_flag flag=0x80000000 fails with EINVAL");
    errno = 0;
    ok (flux_msg_clear_flag (msg, 0x80000000) < 0 && errno == EINVAL,
        "flux_msg_clear_flag flag=0x80000000 fails with EINVAL");
    lives_ok ({flux_msg_has_flag (NULL, FLUX_MSGFLAG_STREAMING);},
       "flux_msg_has_flag msg=NULL does not segfault");

    errno = 0;
    ok (flux_msg_set_flag (msg, FLUX_MSGFLAG_STREAMING
                              | FLUX_MSGFLAG_NORESPONSE) < 0
        && errno == EINVAL,
        "flux_msg_set_flag streaming|noresponse fails with EINVAL");

    errno = 0;
    ok (flux_msg_pack (NULL, "{s:i}", "foo", 42) < 0 && errno == EINVAL,
       "flux_msg_pack msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_pack (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_pack fails with EINVAL with NULL format");
    errno = 0;
    ok (flux_msg_unpack (NULL, "{s:i}", "type", &type) < 0 && errno == EINVAL,
       "flux_msg_unpack msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_unpack (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_unpack fails with EINVAL with NULL format");

    errno = 0;
    ok (flux_msg_set_nodeid (NULL, 0) < 0 && errno == EINVAL,
                "flux_msg_set_nodeid fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_nodeid (NULL, &nodeid) < 0 && errno == EINVAL,
        "flux_msg_get_nodeid fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_nodeid (rsp, &nodeid) < 0 && errno == EPROTO,
        "flux_msg_get_nodeid fails with PROTO on msg != request type");
    errno = 0;
    ok (flux_msg_get_userid (NULL, &cred.userid) < 0 && errno == EINVAL,
        "flux_msg_get_userid msg=NULL fails with EINVAL");
    lives_ok ({flux_msg_get_userid (msg, NULL);},
        "flux_msg_get_userid userid=NULL does not segfault");
    errno = 0;
    ok (flux_msg_set_userid (NULL, cred.userid) < 0 && errno == EINVAL,
        "flux_msg_set_userid msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_get_rolemask (NULL, &cred.rolemask) < 0 && errno == EINVAL,
        "flux_msg_get_rolemask msg=NULL fails with EINVAL");
    lives_ok ({flux_msg_get_rolemask (msg, NULL);},
        "flux_msg_get_rolemask rolemask=NULL does not segfault");
    errno = 0;
    ok (flux_msg_set_rolemask (NULL, cred.rolemask) < 0 && errno == EINVAL,
        "flux_msg_set_rolemask msg=NULL fails with EINVAL");

    errno = 0;
    ok (flux_msg_get_cred (NULL, &cred) < 0 && errno == EINVAL,
        "flux_msg_get_cred msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_get_cred (msg, NULL) < 0 && errno == EINVAL,
        "flux_msg_get_cred cred=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_set_cred (NULL, cred) < 0 && errno == EINVAL,
        "flux_msg_set_cred msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_msg_authorize (NULL, 42) < 0 && errno == EINVAL,
        "flux_msg_authorize msg=NULL fails with EINVAL");

    errno = 0;
    ok (flux_msg_set_errnum (NULL, 42) < 0 && errno == EINVAL,
        "flux_msg_set_errnum on fails with errno == EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_errnum (NULL, &errnum) < 0 && errno == EINVAL,
        "flux_msg_get_errnum fails with EINVAL on msg = NULL");
    lives_ok ({flux_msg_get_errnum (msg, NULL);},
        "flux_msg_get_errnum errnum = NULL does not segfault");
    errno = 0;
    ok (flux_msg_get_errnum (req, &errnum) < 0 && errno == EPROTO,
        "flux_msg_get_errnum fails with EPROTO on msg != response type");
    errno = 0;
    ok (flux_msg_set_seq (NULL, 0) < 0 && errno == EINVAL,
        "flux_msg_set_seq fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_seq (NULL, &seq) < 0 && errno == EINVAL,
        "flux_msg_get_seq fails with EINVAL on msg = NULL");
    lives_ok ({flux_msg_get_seq (msg, NULL);},
        "flux_msg_get_seq seq = NULL does not segfault");
    errno = 0;
    ok (flux_msg_get_seq (req, &seq) < 0 && errno == EPROTO,
        "flux_msg_get_seq fails with EPROTO on msg != event type");
    errno = 0;
    ok (flux_msg_set_control (NULL, 0, 0) < 0 && errno == EINVAL,
        "flux_msg_set_status fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_control (NULL, &type, &status) < 0 && errno == EINVAL,
        "flux_msg_get_status fails with EINVAL on msg = NULL");
    lives_ok ({flux_msg_get_control (msg, &type, NULL);},
        "flux_msg_get_status status = NULL does not segfault");
    errno = 0;
    ok (flux_msg_get_control (req, &type, &status) < 0 && errno == EPROTO,
        "flux_msg_get_status fails with EPROTO on msg != control type");
    errno = 0;
    ok (flux_msg_set_matchtag (NULL, 42) < 0 && errno == EINVAL,
        "flux_msg_set_matchtag fails with EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_get_matchtag (NULL, &tag) < 0 && errno == EINVAL,
        "flux_msg_get_matchtag fails with EINVAL on msg = NULL");
    lives_ok ({flux_msg_get_matchtag (msg, NULL);},
        "flux_msg_get_matchtag matchtag = NULL does not segfault");
    errno = 0;
    ok (flux_msg_get_matchtag (evt, &tag) < 0 && errno == EPROTO,
        "flux_msg_get_matchtag fails with EPROTO on msg != req/rsp type");

    lives_ok ({flux_msg_route_enable (NULL);},
        "flux_msg_route_enable msg=NULL doesnt crash");
    lives_ok ({flux_msg_route_disable (NULL);},
        "flux_msg_route_disable msg=NULL doesnt crash");
    lives_ok ({flux_msg_route_clear (NULL);},
        "flux_msg_route_clear msg=NULL doesnt crash");

    errno = 0;
    ok (flux_msg_route_push (NULL, "foo") == -1 && errno == EINVAL,
        "flux_msg_route_push returns -1 errno EINVAL on msg = NULL");
    errno = 0;
    ok (flux_msg_route_push (msg, NULL) == -1 && errno == EINVAL,
        "flux_msg_route_push returns -1 errno EINVAL on id = NULL");
    errno = 0;
    ok (flux_msg_route_push (msg, "foo") == -1 && errno == EPROTO,
        "flux_msg_route_push returns -1 errno EPROTO on msg w/o routes enabled");
    errno = 0;
    ok (flux_msg_route_delete_last (NULL) == -1 && errno == EINVAL,
        "flux_msg_route_delete_last returns -1 errno EINVAL on id = NULL");
    errno = 0;
    ok (flux_msg_route_delete_last (msg) == -1 && errno == EPROTO,
        "flux_msg_route_delete_last returns -1 errno EPROTO on msg "
        "w/o routes enabled");
    ok (flux_msg_route_first (NULL) == NULL,
        "flux_msg_route_first returns NULL on msg = NULL");
    ok (flux_msg_route_first (msg) == NULL,
        "flux_msg_route_first returns NULL on msg w/o routes enabled");
    ok (flux_msg_route_last (NULL) == NULL,
        "flux_msg_route_last returns NULL on msg = NULL");
    ok (flux_msg_route_last (msg) == NULL,
        "flux_msg_route_last returns NULL on msg w/o routes enabled");
    errno = 0;
    ok ((flux_msg_route_count (NULL) == -1 && errno == EINVAL),
        "flux_msg_route_count returns -1 errno EINVAL on msg = NULL");
    errno = 0;
    ok ((flux_msg_route_count (msg) == -1 && errno == EPROTO),
        "flux_msg_route_count returns -1 errno EPROTO on msg "
        "w/o routes enabled");
    errno = 0;
    ok ((flux_msg_route_string (NULL) == NULL && errno == EINVAL),
        "flux_msg_route_string returns NULL errno EINVAL on msg = NULL");
    errno = 0;
    ok ((flux_msg_route_string (msg) == NULL && errno == EPROTO),
        "flux_msg_route_string returns NULL errno EPROTO on msg "
        "w/o routes enabled");

    flux_msg_destroy (msg);
}

/* flux_msg_route_first, flux_msg_route_last,
 *   flux_msg_route_count on message with variable number of
 *   routing frames
 */
void check_routes (void)
{
    flux_msg_t *msg;
    const char *route;
    char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
        && msg_frames (msg) == 1,
        "flux_msg_create works and creates msg with 1 frame");

    flux_msg_route_clear (msg);
    ok (msg_frames (msg) == 1,
        "flux_msg_route_clear works, is no-op on msg w/o routes enabled");
    flux_msg_route_disable (msg);
    ok (msg_frames (msg) == 1,
        "flux_msg_route_disable works, is no-op on msg w/o routes enabled");
    flux_msg_route_enable (msg);
    ok (msg_frames (msg) == 2,
        "flux_msg_route_enable works, adds one frame on msg w/ routes enabled");
    ok ((flux_msg_route_count (msg) == 0),
        "flux_msg_route_count returns 0 on msg w/o routes");

    ok ((route = flux_msg_route_first (msg)) == NULL,
        "flux_msg_route_first returns NULL on msg w/o routes");
    ok ((route = flux_msg_route_last (msg)) == NULL,
        "flux_msg_route_last returns NULL on msg w/o routes");
    ok (flux_msg_route_push (msg, "sender") == 0 && msg_frames (msg) == 3,
        "flux_msg_route_push works and adds a frame");
    ok ((flux_msg_route_count (msg) == 1),
        "flux_msg_route_count returns 1 on msg w/ id1");

    ok ((route = flux_msg_route_first (msg)) != NULL,
        "flux_msg_route_first works");
    like (route, "sender",
        "flux_msg_route_first returns id on msg w/ id1");

    ok ((route = flux_msg_route_last (msg)) != NULL,
        "flux_msg_route_last works");
    like (route, "sender",
        "flux_msg_route_last returns id on msg w/ id1");

    ok ((s = flux_msg_route_string (msg)) != NULL,
        "flux_msg_route_string works");
    like (s, "sender",
        "flux_msg_route_string returns correct string on msg w/ id1");
    free (s);

    ok (flux_msg_route_push (msg, "router") == 0 && msg_frames (msg) == 4,
        "flux_msg_route_push works and adds a frame");
    ok ((flux_msg_route_count (msg) == 2),
        "flux_msg_route_count returns 2 on msg w/ id1+id2");

    ok ((route = flux_msg_route_first (msg)) != NULL,
        "flux_msg_route_first works");
    like (route, "sender",
        "flux_msg_route_first returns id1 on msg w/ id1+id2");

    ok ((route = flux_msg_route_last (msg)) != NULL,
        "flux_msg_route_last works");
    like (route, "router",
        "flux_msg_route_last returns id2 on message with id1+id2");

    ok ((s = flux_msg_route_string (msg)) != NULL,
        "flux_msg_route_string works");
    like (s, "sender!router",
        "flux_msg_route_string returns correct string on msg w/ id1+id2");
    free (s);

    ok (flux_msg_route_delete_last (msg) == 0 && msg_frames (msg) == 3,
        "flux_msg_route_delete_last works and removed a frame");
    ok (flux_msg_route_count (msg) == 1,
        "flux_msg_route_count returns 1 on message w/ id1");

    flux_msg_route_clear (msg);
    ok (flux_msg_route_count (msg) == 0,
        "flux_msg_route_clear clear routing frames");
    ok (msg_frames (msg) == 2,
        "flux_msg_route_clear did not disable routing frames");

    ok (flux_msg_route_push (msg, "foobar") == 0 && msg_frames (msg) == 3,
        "flux_msg_route_push works and adds a frame after flux_msg_route_clear()");
    ok ((flux_msg_route_count (msg) == 1),
        "flux_msg_route_count returns 1 on msg w/ id1");

    flux_msg_route_disable (msg);
    ok (msg_frames (msg) == 1,
        "flux_msg_route_disable clear routing frames");

    ok (flux_msg_route_push (msg, "boobar") < 0 && errno == EPROTO,
        "flux_msg_route_push fails with EPROTO after flux_msg_route_disable()");

    flux_msg_destroy (msg);

    msg = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    flux_msg_t *msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST);
    if (!msg || !msg2)
        BAIL_OUT ("flux_msg_create failed");
    flux_msg_route_enable (msg);
    flux_msg_route_enable (msg2);
    ok (flux_msg_route_match_first (msg, msg2) == true,
        "flux_msg_route_match_first returns true on messages with no routes");
    if (flux_msg_route_push (msg, "foobar") < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    ok (flux_msg_route_match_first (msg, msg2) == false,
        "flux_msg_route_match_first returns false on route and no route");
    if (flux_msg_route_push (msg2, "foobar") < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    ok (flux_msg_route_match_first (msg, msg2) == true,
        "flux_msg_route_match_first returns true if routes match");
    if (flux_msg_route_push (msg2, "bar") < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    ok (flux_msg_route_match_first (msg, msg2) == true,
        "flux_msg_route_match_first still returns true with more routes pushed");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
}

/* flux_msg_get_topic, flux_msg_set_topic on message with and without routes
 */
void check_topic (void)
{
    flux_msg_t *msg;
    const char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    ok (flux_msg_set_topic (msg, "blorg") == 0,
       "flux_msg_set_topic works");
    ok (flux_msg_get_topic (msg, &s) == 0,
       "flux_msg_get_topic works on msg w/topic");
    like (s, "blorg",
       "and we got back the topic string we set");

    flux_msg_route_enable (msg);
    ok (flux_msg_route_push (msg, "id1") == 0,
        "flux_msg_route_push works");
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

    ok (strlen(flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error() returns empty string before pack/unpack");

    is (flux_msg_last_error (NULL), "msg object is NULL",
        "flux_msg_last_error() returns 'msg object is NULL' on NULL arg");

    /* Unpack on a message with invalid string payload should be an error
     */
    errno = 0;
    ok (flux_msg_set_payload (msg, "fluffy", 6) == 0,
        "set invalid string payload on msg");
    errno = 0;
    ok (flux_msg_unpack (msg, "{s:i}", "foo", &i) < 0 && errno == EPROTO,
        "flux_msg_unpack() on message with invalid payload returns EPROTO");
    is (flux_msg_last_error (msg),
        "flux_msg_get_string: Protocol error",
        "flux_msg_last_error reports '%s'",
        flux_msg_last_error(msg));

    /* RFC 3 - json payload must be an object
     * Encoding should return EINVAL.
     */
    errno = 0;
    ok (flux_msg_pack (msg, "[1,2,3]") < 0 && errno == EINVAL,
       "flux_msg_pack array fails with EINVAL");
    ok (strlen(flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));
    errno = 0;
    ok (flux_msg_pack (msg, "3.14") < 0 && errno == EINVAL,
       "flux_msg_pack scalar fails with EINVAL");
    ok (strlen(flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));

    /* Sneak in a malformed JSON payloads and test decoding.
     * 1) array
     */
    if (flux_msg_set_string (msg, "[1,2,3]") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack array fails with EPROTO");
    ok (strlen(flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));
    /* 2) bare value
     */
    if (flux_msg_set_string (msg, "3.14") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack scalar fails with EPROTO");
    ok (strlen(flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));
    /* 3) malformed object (no trailing })
     */
    if (flux_msg_set_string (msg, "{\"a\":42") < 0)
        BAIL_OUT ("flux_msg_set_string failed");
    errno = 0;
    ok (flux_msg_unpack (msg, "o", &o) < 0 && errno == EPROTO,
        "flux_msg_unpack malformed object fails with EPROTO");
    ok (strlen(flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));

    ok (flux_msg_pack (msg, "{s:i}", "foo", 42) == 0,
       "flux_msg_pack works");
    ok (strlen(flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error returns empty string after ok pack");
    i = 0;
    ok (flux_msg_unpack (msg, "{s:i}", "foo", &i) == 0 && i == 42,
       "flux_msg_unpack returns payload intact");
    ok (strlen(flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error returns empty string after ok unpack");

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
    ok (strlen (flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));

    errno = 0;
    ok (flux_msg_pack (msg, "[i,i,i]", 1,2,3) < 0 && errno == EINVAL,
        "flux_msg_pack array fails with EINVAL");
    is (flux_msg_last_error (msg), "payload is not a JSON object",
        "flux_msg_last_error: %s", flux_msg_last_error (msg));
    errno = 0;
    ok (flux_msg_pack (msg, "i", 3.14) < 0 && errno == EINVAL,
       "flux_msg_pack scalar fails with EINVAL");
    ok (strlen (flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error: %s", flux_msg_last_error (msg));
    ok (flux_msg_pack (msg, "{s:i, s:s}", "foo", 42, "bar", "baz") == 0,
       "flux_msg_pack object works");
    ok (strlen (flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error is empty string after ok pack");
    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_unpack object works");
    ok (strlen (flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error is empty string after ok unpack");
    ok (i == 42 && s != NULL && streq (s, "baz"),
        "decoded content matches encoded content");

    /* reset payload */
    ok (flux_msg_pack (msg, "{s:i, s:s}", "foo", 43, "bar", "smurf") == 0,
       "flux_msg_pack can replace JSON object payload");
    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:i, s:s}", "foo", &i, "bar", &s) == 0,
       "flux_msg_unpack object works");
    ok (i == 43 && s != NULL && streq (s, "smurf"),
        "decoded content matches new encoded content");

    i = 0;
    s = NULL;
    ok (flux_msg_unpack (msg, "{s:s, s:i}", "bar", &s, "foo", &i) == 0,
       "flux_msg_unpack object works out of order");
    ok (i == 43 && s != NULL && streq (s, "smurf"),
        "decoded content matches new encoded content");

    ok (strlen (flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error is empty string on EINVAL");

    errno = 0;
    ok (flux_msg_unpack (msg, "") < 0 && errno == EINVAL,
        "flux_msg_unpack fails with EINVAL with \"\" format");
    ok (strlen (flux_msg_last_error (msg)) == 0,
        "flux_msg_last_error is empty string on EINVAL");

    errno = 0;
    ok (flux_msg_unpack (msg, "{s:s}", "nope", &s) < 0 && errno == EPROTO,
        "flux_msg_unpack fails with EPROTO with nonexistent key");
    ok (strlen (flux_msg_last_error (msg)) > 0,
        "flux_msg_last_error is %s", flux_msg_last_error (msg));

    /* flux_msg_pack/unpack doesn't reject packed NUL chars */
    char buf[4] = "foo";
    char *result = NULL;
    size_t len = -1;

    ok (flux_msg_pack (msg, "{ss#}", "result", buf, 4) == 0,
        "flux_msg_pack with NUL char works");
    ok (flux_msg_unpack (msg, "{ss%}", "result", &result, &len) == 0,
        "flux_msg_unpack with NUL char works");
    ok (len == 4,
        "flux_msg_unpack returned correct length");
    ok (memcmp (buf, result, 4) == 0,
        "original buffer and result match");

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
    size_t plen = sizeof (pay);
    size_t len;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "flux_msg_create works");
    errno = 0;
    ok (flux_msg_set_payload (msg, NULL, 0) == 0 && errno == 0,
        "flux_msg_set_payload NULL works with no payload");
    errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload still fails");

    errno = 0;
    memset (pay, 42, plen);
    ok (flux_msg_set_payload (msg, pay, plen) == 0
        && msg_frames (msg) == 2,
       "flux_msg_set_payload works");

    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && msg_frames (msg) == 3,
       "flux_msg_set_topic works");
    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload works with topic");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");
    ok (flux_msg_set_topic (msg, NULL) == 0 && msg_frames (msg) == 2,
       "flux_msg_set_topic NULL works");

    flux_msg_route_enable (msg);
    ok (msg_frames (msg) == 3,
        "flux_msg_route_enable works");
    ok (flux_msg_route_push (msg, "id1") == 0 && msg_frames (msg) == 4,
        "flux_msg_route_push works");

    len = 0; buf = NULL; errno = 0;
    ok (flux_msg_get_payload (msg, &buf, &len) == 0
        && buf && len == plen && errno == 0,
       "flux_msg_get_payload still works, with routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (msg, "blorg") == 0 && msg_frames (msg) == 5,
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
 *
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

    flux_msg_destroy (msg);
}

void check_security (void)
{
    flux_msg_t *msg;
    struct flux_msg_cred cred;
    struct flux_msg_cred user_9 = { .rolemask = FLUX_ROLE_USER, .userid = 9 };
    struct flux_msg_cred owner_2 = { .rolemask = FLUX_ROLE_OWNER, .userid = 2 };
    struct flux_msg_cred user_unknown = { .rolemask = FLUX_ROLE_USER,
                                          .userid = FLUX_USERID_UNKNOWN  };
    struct flux_msg_cred none_9 = { .rolemask = FLUX_ROLE_NONE, .userid = 9 };

    /* Accessors work
     */
    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_userid (msg, &cred.userid) == 0
        && cred.userid == FLUX_USERID_UNKNOWN,
        "message created with userid=FLUX_USERID_UNKNOWN");
    ok (flux_msg_get_rolemask (msg, &cred.rolemask) == 0
        && cred.rolemask == FLUX_ROLE_NONE,
        "message created with rolemask=FLUX_ROLE_NONE");
    ok (flux_msg_set_userid (msg, 4242) == 0
        && flux_msg_get_userid (msg, &cred.userid) == 0
        && cred.userid == 4242,
        "flux_msg_set_userid 4242 works");
    ok (flux_msg_set_rolemask (msg, FLUX_ROLE_ALL) == 0
        && flux_msg_get_rolemask (msg, &cred.rolemask) == 0
        && cred.rolemask == FLUX_ROLE_ALL,
        "flux_msg_set_rolemask FLUX_ROLE_ALL works");

    memset (&cred, 0, sizeof (cred));
    ok (flux_msg_get_cred (msg, &cred) == 0
        && cred.userid == 4242 && cred.rolemask == FLUX_ROLE_ALL,
        "flux_msg_get_cred works");

    ok (flux_msg_set_cred (msg, user_9) == 0
        && flux_msg_get_cred (msg, &cred) == 0
        && cred.userid == user_9.userid
        && cred.rolemask == user_9.rolemask,
        "flux_msg_set_cred works");

    /* Simple authorization works
     */
    ok (flux_msg_cred_authorize (owner_2, 2) == 0,
        "flux_msg_cred_authorize allows owner when userids match");
    ok (flux_msg_cred_authorize (owner_2, 4) == 0,
        "flux_msg_cred_authorize allows owner when userids mismatch");
    ok (flux_msg_cred_authorize (user_9, 9) == 0,
        "flux_msg_cred_authorize allows guest when userids match");
    errno = 0;
    ok (flux_msg_cred_authorize (user_9, 10) < 0
        && errno == EPERM,
        "flux_msg_cred_authorize denies guest (EPERM) when userids mismatch");
    errno = 0;
    ok (flux_msg_cred_authorize (user_unknown, FLUX_USERID_UNKNOWN) < 0
        && errno == EPERM,
        "flux_msg_cred_authorize denies guest (EPERM) when userids=UNKNOWN");
    errno = 0;
    ok (flux_msg_cred_authorize (none_9, 9) < 0
        && errno == EPERM,
        "flux_msg_cred_authorize denies guest (EPERM) when role=NONE");

    /* Repeat with the message version
     */
    if (flux_msg_set_cred (msg, owner_2) < 0)
        BAIL_OUT ("flux_msg_set_cred failed");
    ok (flux_msg_authorize (msg, 2) == 0,
        "flux_msg_authorize allows owner when userid's match");
    ok (flux_msg_authorize (msg, 4) == 0,
        "flux_msg_authorize allows owner when userid's mismatch");
    if (flux_msg_set_cred (msg, user_9) < 0)
        BAIL_OUT ("flux_msg_set_cred failed");
    ok (flux_msg_authorize (msg, 9) == 0,
        "flux_msg_authorize allows guest when userid's match");
    errno = 0;
    ok (flux_msg_authorize (msg, 10) < 0
        && errno == EPERM,
        "flux_msg_authorize denies guest (EPERM) when userid's mismatch");
    if (flux_msg_set_cred (msg, user_unknown) < 0)
        BAIL_OUT ("flux_msg_set_cred failed");
    errno = 0;
    ok (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0
        && errno == EPERM,
        "flux_msg_authorize denies guest (EPERM) when userids=UNKNOWN");
    if (flux_msg_set_cred (msg, none_9) < 0)
        BAIL_OUT ("flux_msg_set_cred failed");
    errno = 0;
    ok (flux_msg_authorize (msg, 9) < 0
        && errno == EPERM,
        "flux_msg_authorize denies guest (EPERM) when role=NONE");

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
        "flux_msg_cmp with globbed '*' topic works");

    match.topic_glob = "hello.fo?";
    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp with globbed '?' topic works");

    match.topic_glob = "hello.fo[op]";
    ok (flux_msg_cmp (msg, match),
        "flux_msg_cmp with globbed '[' topic works");
    flux_msg_destroy (msg);
}

void check_encode (void)
{
    flux_msg_t *msg, *msg2;
    uint8_t smallbuf[1];
    void *buf;
    size_t size;
    const char *topic;
    int type;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_set_topic (msg, "foo.bar") == 0,
        "flux_msg_set_topic works");
    errno = 0;
    ok (flux_msg_encode (msg, smallbuf, 1) < 0 && errno == EINVAL,
        "flux_msg_encode fails on EINVAL with buffer too small");
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
    ok (flux_msg_get_topic (msg2, &topic) == 0 && streq (topic, "foo.bar"),
        "decoded expected topic string");
    ok (flux_msg_has_payload (msg2) == false,
        "decoded expected (lack of) payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
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
    ok (flux_msg_aux_set (msg, "test", test_data, myfree) == 0,
        "hang aux data member on message with destructor");
    ok (flux_msg_aux_get (msg, "incorrect") == NULL,
        "flux_msg_aux_get for unknown key returns NULL");
    ok (flux_msg_aux_get (msg, "test") == test_data,
        "flux_msg_aux_get aux data member key returns orig pointer");
    flux_msg_destroy (msg);
    ok (myfree_arg == test_data,
        "destroyed message and aux destructor was called");
}

void check_copy (void)
{
    flux_msg_t *msg, *cpy;
    int type;
    const char *topic;
    size_t cpylen;
    const char buf[] = "xxxxxxxxxxxxxxxxxx";
    const void *cpybuf;
    const char *s;

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_CONTROL)) != NULL,
        "created no-payload control");
    ok ((cpy = flux_msg_copy (msg, true)) != NULL,
        "flux_msg_copy works");
    flux_msg_destroy (msg);
    type = -1;
    ok (flux_msg_get_type (cpy, &type) == 0&& type == FLUX_MSGTYPE_CONTROL
             && !flux_msg_has_payload (cpy)
             && flux_msg_route_count (cpy) < 0
             && flux_msg_get_topic (cpy, &topic) < 0,
        "copy is keepalive: no routes, topic, or payload");
    flux_msg_destroy (cpy);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created request");
    flux_msg_route_enable (msg);
    ok (flux_msg_route_push (msg, "first") == 0,
        "added route 1");
    ok (flux_msg_route_push (msg, "second") == 0,
        "added route 2");
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
             && flux_msg_route_count (cpy) == 2
             && flux_msg_get_topic (cpy, &topic) == 0 && streq (topic,"foo"),
        "copy is request: w/routes, topic, and payload");

    ok ((s = flux_msg_route_last (cpy)) != NULL,
        "flux_msg_route_last gets route from copy");
    like (s, "second",
          "flux_msg_route_last returns correct second route");
    ok (flux_msg_route_delete_last (cpy) == 0,
        "flux_msg_route_delete_last removes second route");

    ok ((s = flux_msg_route_last (cpy)) != NULL,
        "flux_msg_route_last pops route from copy");
    like (s, "first",
          "flux_msg_route_last returns correct first route");
    ok (flux_msg_route_delete_last (cpy) == 0,
        "flux_msg_route_delete_last removes first route");

    flux_msg_destroy (cpy);

    ok ((cpy = flux_msg_copy (msg, false)) != NULL,
        "flux_msg_copy works (payload=false)");
    type = -1;
    ok (flux_msg_get_type (cpy, &type) == 0 && type == FLUX_MSGTYPE_REQUEST
             && !flux_msg_has_payload (cpy)
             && flux_msg_route_count (cpy) == 2
             && flux_msg_get_topic (cpy, &topic) == 0 && streq (topic,"foo"),
        "copy is request: w/routes, topic, and no payload");
    flux_msg_destroy (cpy);
    flux_msg_destroy (msg);
}

void check_print (void)
{
    flux_msg_t *msg;
    char *strpayload = "a.special.payload";
    char buf[] = "xxxxxxxx";
    char buf_long[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    FILE *f = verbose ? stderr : fopen ("/dev/null", "w");
    if (!f)
        BAIL_OUT ("cannot open /dev/null for writing");

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_CONTROL)) != NULL,
        "created test message");
    lives_ok ({flux_msg_fprint_ts (f, msg, 0.);},
        "flux_msg_fprint_ts doesn't segfault on control");
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
    flux_msg_route_enable (msg);
    ok (flux_msg_route_push (msg, "id1") == 0,
        "added one route");
    ok (flux_msg_set_payload (msg, buf, strlen (buf)) == 0,
        "added payload");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on fully loaded request");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    ok (flux_msg_set_userid (msg, 42) == 0,
        "set userid");
    ok (flux_msg_set_rolemask (msg, FLUX_ROLE_OWNER) == 0,
        "set rolemask");
    ok (flux_msg_set_nodeid (msg, 42) == 0,
        "set nodeid");
    ok (flux_msg_set_string (msg, strpayload) == 0,
        "added payload");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on request settings #1");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    ok (flux_msg_set_rolemask (msg, FLUX_ROLE_USER) == 0,
        "set rolemask");
    ok (flux_msg_set_flag (msg, FLUX_MSGFLAG_NORESPONSE) == 0
        && flux_msg_set_flag (msg, FLUX_MSGFLAG_UPSTREAM) == 0,
        "set new flags");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on request settings #2");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    ok (flux_msg_set_rolemask (msg, FLUX_ROLE_ALL) == 0,
        "set rolemask");
    ok (flux_msg_set_flag (msg, FLUX_MSGFLAG_PRIVATE) == 0
        && flux_msg_set_flag (msg, FLUX_MSGFLAG_STREAMING) == 0,
        "set new flags");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on request settings #3");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");
    ok (flux_msg_set_payload (msg, buf_long, strlen (buf_long)) == 0,
        "added long payload");
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on long payload");
    flux_msg_destroy (msg);

    ok ((msg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "created test message");
    flux_msg_route_enable (msg);
    lives_ok ({flux_msg_fprint (f, msg);},
        "flux_msg_fprint doesn't segfault on response with empty route stack");
    flux_msg_destroy (msg);

    fclose (f);
}

void check_print_rolemask (void)
{
    flux_msg_t *msg;
    FILE *fp;
    uint32_t rolemask;
    char *buf = NULL;
    size_t size = 0;

    rolemask = FLUX_ROLE_LOCAL | FLUX_ROLE_USER | 0x10;
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST))
        || flux_msg_set_rolemask (msg, rolemask) < 0)
        BAIL_OUT ("failed to create test request");
    if (!(fp = open_memstream (&buf, &size)))
        BAIL_OUT ("open_memstream failed");
    flux_msg_fprint (fp, msg);
    fclose (fp); // close flushes content
    diag ("%s", buf);
    ok (buf && strstr (buf, "rolemask=user,local,0x10") != NULL,
        "flux_msg_fprint() rolemask string is correct");
    free (buf);
    flux_msg_destroy (msg);
}

void check_flags (void)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    ok (msg->proto.flags == 0,
        "flags are initially zero");

    /* FLUX_MSGFLAG_USER1 */
    ok (flux_msg_has_flag (msg, FLUX_MSGFLAG_USER1) == false,
        "flux_msg_has_flag FLUX_MSGFLAG_USER1 = false");
    ok (flux_msg_set_flag (msg, FLUX_MSGFLAG_USER1) == 0,
        "flux_msg_set_flag FLUX_MSGFLAG_USER1 works");
    ok (flux_msg_has_flag (msg, FLUX_MSGFLAG_USER1) == true,
        "flux_msg_has_flag FLUX_MSGFLAG_USER1 = true");

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

    /* FLUX_MSGFLAG_NORESPONSE */
    ok (flux_msg_is_noresponse (msg) == false,
        "flux_msg_is_noresponse = false");
    ok (flux_msg_set_noresponse (msg) == 0,
        "flux_msg_set_noresponse_works");
    ok (flux_msg_is_noresponse (msg) == true,
        "flux_msg_is_noresponse = true");

    /* noresponse and streaming are mutually exclusive */
    ok (flux_msg_set_streaming (msg) == 0
        && flux_msg_set_noresponse (msg) == 0
        && flux_msg_is_streaming (msg) == false
        && flux_msg_is_noresponse (msg) == true,
        "flux_msg_set_noresponse clears streaming flag");
    ok (flux_msg_set_noresponse (msg) == 0
        && flux_msg_set_streaming (msg) == 0
        && flux_msg_is_noresponse (msg) == false
        && flux_msg_is_streaming (msg) == true,
        "flux_msg_set_streaming clears noresponse flag");

    ok (flux_msg_set_topic (msg, "foo") == 0
        && flux_msg_has_flag (msg, FLUX_MSGFLAG_TOPIC),
        "flux_msg_set_topic sets FLUX_MSGFLAG_TOPIC");

    ok (flux_msg_set_payload (msg, "foo", 3) == 0
        && flux_msg_has_flag (msg, FLUX_MSGFLAG_PAYLOAD),
        "flux_msg_set_payload sets FLUX_MSGFLAG_PAYLOAD");

    flux_msg_route_enable (msg);
    ok (flux_msg_has_flag (msg, FLUX_MSGFLAG_ROUTE),
        "flux_msg_route_enable sets FLUX_MSGFLAG_ROUTE");

    flux_msg_destroy (msg);
}

void check_refcount (void)
{
    flux_msg_t *msg;
    const flux_msg_t *p;
    int type;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_CONTROL)))
        BAIL_OUT ("failed to create test message");
    p = flux_msg_incref (msg);
    ok (p == msg,
        "flux_msg_incref returns pointer to original");
    flux_msg_destroy (msg);
    ok (flux_msg_get_type (p, &type) == 0 && type == FLUX_MSGTYPE_CONTROL,
        "reference remains valid after destroy");
    flux_msg_decref (p);
}

struct pvec {
    const char *desc;
    struct proto p;
    uint8_t buf[PROTO_SIZE];
};

// N.B. RFC 3 describes this encoding
// 4-byte integers are encoded in network order (big endian = MSB first)
static struct pvec testvec[] = {
    { "fake test message",
      { .type = 0xab, .flags = 0xcd,
        .userid = 0x00010203, .rolemask = 0x04050607,
        .aux1 = 0x08090a0b, .aux2 = 0x0c0d0e0f },
      { PROTO_MAGIC, PROTO_VERSION, 0xab, 0xcd,
        0x00, 0x01, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b,
        0x0c, 0x0d, 0x0e, 0x0f }
    },
    { "overlay control disconnect",
      { .type = FLUX_MSGTYPE_CONTROL, .flags = 0,
        .userid = 100, .rolemask = FLUX_ROLE_OWNER,
        .control_type = 2, .control_status = 0 },
      { PROTO_MAGIC, PROTO_VERSION, 0x08, 0,
        0, 0, 0, 100,
        0, 0, 0, 1,
        0, 0, 0, 2,
        0, 0, 0, 0 }
    },
    { "hello request",
      { .type = FLUX_MSGTYPE_REQUEST,
        .flags = FLUX_MSGFLAG_TOPIC | FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_ROUTE,
        .userid = 100, .rolemask = FLUX_ROLE_OWNER,
        .nodeid = FLUX_NODEID_ANY, .matchtag = 0 },
      { PROTO_MAGIC, PROTO_VERSION, 0x01, 0x0b,
        0, 0, 0, 100,
        0, 0, 0, 1,
        0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0 }
    },
    { "hello response",
      { .type = FLUX_MSGTYPE_RESPONSE,
        .flags = FLUX_MSGFLAG_TOPIC | FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_ROUTE,
        .userid = 100, .rolemask = FLUX_ROLE_OWNER,
        .nodeid = FLUX_NODEID_ANY, .matchtag = 0 },
      { PROTO_MAGIC, PROTO_VERSION, 0x02, 0x0b,
        0, 0, 0, 100,
        0, 0, 0, 1,
        0xff, 0xff, 0xff, 0xff,
        0, 0, 0, 0 }
    },
};

int check_proto_encode (const struct pvec *pvec)
{
    uint8_t buf[PROTO_SIZE];

    if (proto_encode (&pvec->p, buf, sizeof (buf)) < 0) {
        diag ("proto_encode returned -1");
        return -1;
    }
    int errors = 0;
    for (int i = 0; i < sizeof (buf); i++) {
        if (buf[i] != pvec->buf[i]) {
            diag ("buf[%d]=0x%x != 0x%x", i, buf[i], pvec->buf[i]);
            errors++;
        }
    }
    if (errors > 0)
        return -1;
    return 0;
}
int check_proto_decode (const struct pvec *pvec)
{
    struct proto p;
    memset (&p, 0, sizeof (p));
    if (proto_decode (&p, pvec->buf, PROTO_SIZE) < 0) {
        diag ("proto_decode returned -1");
        return -1;
    }
    int errors = 0;
    if (p.type != pvec->p.type) {
        diag ("proto->type=0x%x != 0x%x", p.type, pvec->p.type);
        errors++;
    }
    if (p.flags != pvec->p.flags) {
        diag ("proto->flags=0x%x != 0x%x", p.flags, pvec->p.flags);
        errors++;
    }
    if (p.userid != pvec->p.userid) {
        diag ("proto->userid=0x%x != 0x%x", p.userid, pvec->p.userid);
        errors++;
    }
    if (p.rolemask != pvec->p.rolemask) {
        diag ("proto->rolemask=0x%x != 0x%x", p.rolemask, pvec->p.rolemask);
        errors++;
    }
    if (p.aux1 != pvec->p.aux1) {
        diag ("proto->aux1=0x%x != 0x%x", p.aux1, pvec->p.aux1);
        errors++;
    }
    if (p.aux2 != pvec->p.aux2) {
        diag ("proto->aux2=0x%x != 0x%x", p.aux2, pvec->p.aux2);
        errors++;
    }
    if (errors > 0)
        return -1;
    return 0;
}

void check_proto_internal (void)
{
    for (int i = 0; i < ARRAY_SIZE (testvec); i++) {
        ok (check_proto_encode (&testvec[i]) == 0,
           "proto encode worked on %s", testvec[i].desc);
        ok (check_proto_decode (&testvec[i]) == 0,
           "proto decode worked on %s", testvec[i].desc);
    }
}

int main (int argc, char *argv[])
{
    int opt;

    while ((opt = getopt (argc, argv, "v")) != -1) {
        if (opt == 'v')
            verbose = true;
    }

    plan (NO_PLAN);

    check_cornercase ();
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

    check_refcount();

    check_print ();
    check_print_rolemask ();

    check_proto_internal ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

