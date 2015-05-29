#include "src/common/libflux/handle.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/event.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    const char *topic;
    uint32_t matchtag, matchtag2;
    uint32_t nodeid;
    int flags;
    flux_t h;

    plan (18);

    /* send/recv without payload */
    /* N.B. normally FLUX_CONNECTOR_PATH is set by flux command driver */
    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open successfully opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    ok ((zmsg = flux_request_encode ("a.b.c", NULL)) != NULL,
        "message encoded with no payload");
    ok (flux_request_send (h, NULL, &zmsg) == 0 && zmsg == NULL,
        "message sent to loop with matchtag==NULL");
    ok ((zmsg = flux_request_recv (h, false)) != NULL,
        "message received from loop");
    topic = NULL;
    ok (flux_request_decode (zmsg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "a.b.c"),
        "flux_request_decode OK");
    matchtag = 123456UL;
    ok (flux_msg_get_matchtag (zmsg, &matchtag) == 0
        && matchtag == FLUX_MATCHTAG_NONE,
        "matchtag is FLUX_MATCHTAG_NONE");
    ok (flux_request_send (h, &matchtag, &zmsg) == 0 && zmsg == NULL
        && matchtag != FLUX_MATCHTAG_NONE,
        "message resent to loop with matchtag set");
    ok ((zmsg = flux_request_recv (h, false)) != NULL,
        "message received from loop");
    matchtag2 = 123456UL;
    ok (flux_msg_get_matchtag (zmsg, &matchtag2) == 0
        && matchtag2 == matchtag,
        "matchtag correctly decoded");
    ok (flux_request_send (h, NULL, &zmsg) == 0 && zmsg == NULL,
        "message resent to loop with matchtag==NULL");
    ok ((zmsg = flux_request_recv (h, false)) != NULL,
        "message received from loop");
    matchtag2 = 123456UL;
    ok (flux_msg_get_matchtag (zmsg, &matchtag2) == 0
        && matchtag2 == matchtag,
        "matchtag from last time was undisturbed");
    ok (flux_request_sendto (h, NULL, &zmsg, 42) == 0 && zmsg == NULL,
        "message resent to loop with nodeid==42");
    ok ((zmsg = flux_request_recv (h, false)) != NULL,
        "message received from loop");
    nodeid = 0xfffff;
    flags  = 0xfffff;
    ok (flux_msg_get_nodeid (zmsg, &nodeid, &flags) == 0
        && nodeid == 42 && flags == 0,
        "nodeid correctly decoded");
    ok (flux_request_sendto (h, NULL, &zmsg, FLUX_NODEID_UPSTREAM) == 0
        && zmsg == NULL,
        "message resent to loop with nodeid==FLUX_NODEID_UPSTREAM");
    ok ((zmsg = flux_request_recv (h, false)) != NULL,
        "message received from loop");
    nodeid = 0xfffff;
    flags  = 0xfffff;
    /* N.B. loop connector hardwires the nodeid to 0 */
    ok (flux_msg_get_nodeid (zmsg, &nodeid, &flags) == 0
        && nodeid == 0 && flags == FLUX_MSGFLAG_UPSTREAM,
        "upstream nodeid and flags correctly decoded");

    zmsg_destroy (&zmsg);
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

