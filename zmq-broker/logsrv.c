/* logsrv.c - aggregate log data */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmbd.h"
#include "log.h"
#include "plugin.h"

#include "logsrv.h"

static void _recv_log_msg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *mo, *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0)
        goto done;
    if (!o || !(mo = json_object_object_get (o, "message")))
        goto done;
    msg ("%s", json_object_get_string (mo));
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    if (cmb_msg_match (*zmsg, "log.msg"))
        _recv_log_msg (p, zmsg);
}

static void _init (plugin_ctx_t *p)
{
}

struct plugin_struct logsrv = {
    .name      = "log",
    .initFn    = _init,
    .recvFn    = _recv,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
