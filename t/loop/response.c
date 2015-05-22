#include "src/common/libflux/handle.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    const char *topic;
    uint32_t matchtag;
    flux_t h;

    plan (10);

    /* send/recv */
    /* N.B. normally FLUX_CONNECTOR_PATH is set by flux command driver */
    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open successfully opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    ok ((zmsg = flux_response_encode ("a.b.c", 0, NULL)) != NULL,
        "flux_response_encode works");
    ok (flux_response_send (h, &zmsg) == 0 && zmsg == NULL,
        "flux_response_send works");
    ok ((zmsg = flux_response_recv (h, 42, true)) == NULL
        && errno == EWOULDBLOCK,
        "flux_response_recv nonblock on wrong matchtag returns EWOULDBLOCK");
    ok ((zmsg = flux_response_recv (h, FLUX_MATCHTAG_NONE, false)) != NULL,
        "flux_response_recv FLUX_MATCHTAG_NONE works");
    topic = NULL;
    ok (flux_response_decode (zmsg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "a.b.c"),
        "flux_response_decode works");
    ok ((matchtag = flux_matchtag_alloc (h, 1)) != FLUX_MATCHTAG_NONE
        && flux_msg_set_matchtag (zmsg, matchtag) == 0,
        "allocated and set a matchtag in message");
    ok (flux_response_send (h, &zmsg) == 0 && zmsg == NULL,
        "flux_response_send works");
    ok ((zmsg = flux_response_recv (h, matchtag +  1, true)) == NULL,
        "flux_response_recv nonblock with non-matching matchtag fails");
    ok ((zmsg = flux_response_recv (h, matchtag, false)) != NULL,
        "flux_response_recv with matching matchtag works");

    zmsg_destroy (&zmsg);
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

