#include "src/common/libflux/handle.h"
#include "src/common/libflux/event.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    const char *topic;
    flux_t h;

    plan (5);

    /* send/recv */
    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "flux_open successfully opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    ok ((zmsg = flux_event_encode ("foo.bar", NULL)) != NULL,
        "flux_event_encode works");
    ok (flux_event_send (h, &zmsg) == 0 && zmsg == NULL,
        "flux_event_send works");
    ok ((zmsg = flux_event_recv (h, false)) != NULL,
        "flux_event_recv works");
    topic = NULL;
    ok (flux_event_decode (zmsg, &topic, NULL) == 0
        && topic != NULL && !strcmp (topic, "foo.bar"),
        "flux_event_decode works");

    zmsg_destroy (&zmsg);
    flux_close (h);


    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

