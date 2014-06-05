
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

/* Copy input arguments to output arguments and respond to RPC.
 */
static int mecho_mrpc_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    json_object *request = NULL;
    json_object *inarg = NULL;
    flux_mrpc_t f = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0) {
        flux_log (h, LOG_ERR, "cmb_msg_decode: %s", strerror (errno));
        goto done;
    }
    if (!request) {
        flux_log (h, LOG_ERR, "missing JSON part");
        goto done;
    }
    if (!(f = flux_mrpc_create_fromevent (h, request))) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (h, LOG_ERR, "flux_mrpc_create_fromevent: %s",
                                    strerror (errno));
        goto done;
    }
    if (flux_mrpc_get_inarg (f, &inarg) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_get_inarg: %s", strerror (errno));
        goto done;
    }
    flux_mrpc_put_outarg (f, inarg);
    if (flux_mrpc_respond (f) < 0) {
        flux_log (h, LOG_ERR, "flux_mrpc_respond: %s", strerror (errno));
        goto done;
    }
done:
    if (request)
        json_object_put (request);
    if (inarg)
        json_object_put (inarg);
    if (f)
        flux_mrpc_destroy (f);
    zmsg_destroy (zmsg);
    return 0;
}

int mod_main (flux_t h, zhash_t *args)
{
    if (flux_event_subscribe (h, "mrpc.mecho") < 0) {
        flux_log (h, LOG_ERR, "%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }
    if (flux_msghandler_add (h, FLUX_MSGTYPE_EVENT, "mrpc.mecho",
                                                    mecho_mrpc_cb, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
