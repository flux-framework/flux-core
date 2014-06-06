
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "log.h"
#include "util.h"
#include "plugin.h"

static json_object * json_echo (const char *s, int id)
{
    json_object *o = util_json_object_new_object ();

    if (o == NULL)
        return NULL;

    util_json_object_add_string (o, "string", s);
    util_json_object_add_int (o, "id", id);
    return o;
}

static int echo_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    json_object *o = NULL;
    if (cmb_msg_decode (*zmsg, NULL, &o) >= 0) {
        json_object *so = json_object_object_get (o, "string");
        json_object *no = json_object_object_get (o, "repeat");
        const char *s;
        int i, repeat;

        if (so == NULL || no == NULL)
            goto out;

        s = json_object_get_string (so);
        repeat = json_object_get_int (no);

        for (i = 0; i < repeat; i++) {
            zmsg_t *z;
            json_object *respo;
            if (!(z = zmsg_dup (*zmsg)))
                oom ();

            respo = json_echo (s, flux_rank (h));
            cmb_msg_replace_json (z, respo);
            flux_response_sendmsg (h, &z);
            json_object_put (respo);
        }
    }

out:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
    return 0;
}

int mod_main (flux_t h, zhash_t *args)
{
    if (flux_msghandler_add (h, FLUX_MSGTYPE_EVENT, "echo",
                             echo_request_cb, NULL) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("echo");

/*
 * vi: ts=4 sw=4 expandtab
 */
