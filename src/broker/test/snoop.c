#include <flux/core.h>
#include <czmq.h>
#include <stdio.h>

#include "snoop.h"

#include "src/common/libtap/tap.h"

int main (int argc, char **argv)
{
    snoop_t *snoop;
    zctx_t *zctx;
    const char *uri;
    void *zs;
    flux_msg_t *msg, *msg2;
    int type, rc;

    plan (7);

    ok ((zctx = zctx_new ()) != NULL,
        "zctx_new works");
    ok ((snoop = snoop_create()) != NULL,
        "snoop_create works");

    snoop_set_zctx (snoop, zctx);
    snoop_set_uri (snoop, "ipc://*");

    ok ((uri = snoop_get_uri (snoop)) != NULL
        && strcmp (uri, "ipc://*") != 0,
        "snoop_get_uri works");

    ok ((zs = zsocket_new (zctx, ZMQ_SUB))
        && zsocket_connect (zs, "%s", uri) == 0,
        "connected to snoop socket %s", uri);
    zsocket_set_subscribe (zs, "");

    /* connect is asynchronous and messages prior to connect/subscribe
     * will be dropped on sender, so send messages until snoop socket
     * starts receiving
     */
    ok ((msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
        "created test message");

    rc = 0;
    while (rc == 0) {
        zmq_pollitem_t zp = { .socket = zs, .events = ZMQ_POLLIN };
        if ((rc = snoop_sendmsg (snoop, msg)) < 0)
            break;
        if ((rc = zmq_poll (&zp, 1, 1)) < 0) /* 1ms timeout */
            break;
    }
    ok (rc == 1,
        "snoop socket is finally ready");
    ok ((msg2 = flux_msg_recvzsock (zs)) != NULL
        && flux_msg_get_type (msg2, &type) == 0
        && type == FLUX_MSGTYPE_REQUEST,
        "received test message on snoop socket");
   
    flux_msg_destroy (msg); 
    flux_msg_destroy (msg2); 
    snoop_destroy (snoop);
    zctx_destroy (&zctx);

    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
