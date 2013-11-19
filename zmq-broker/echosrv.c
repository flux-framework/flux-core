
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
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

static void handle_recv (flux_t h, zmsg_t **zmsg, zmsg_type_t type)
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
}

const struct plugin_ops ops = {
    .recv = handle_recv,
};

/*
 * vi: ts=4 sw=4 expandtab
 */
