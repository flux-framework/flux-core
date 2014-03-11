/* eventcli.c - client code for eventsrv */

#define _GNU_SOURCE
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
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "flux.h"
#include "event.h"

int flux_event_pub (flux_t h, const char *topic, json_object *payload)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int ret = -1;

    util_json_object_add_string (request, "topic", topic);
    if (payload)
        json_object_get (payload);
    json_object_object_add (request, "payload", payload ? payload 
                                    : util_json_object_new_object ());
    errno = 0;
    response = flux_rpc (h, request, "event.pub");
    if (response) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return ret;
}

int flux_event_geturi (flux_t h, char **urip)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    char hostname[HOST_NAME_MAX + 1];
    pid_t pid = getpid ();
    int ret = -1;
    const char *uri;

    if (gethostname (hostname, HOST_NAME_MAX) < 0)
        err_exit ("gethostname");
    util_json_object_add_int (request, "pid", pid);
    util_json_object_add_string (request, "hostname", hostname);
    if (!(response = flux_rpc (h, request, "event.geturi")))
        goto done;
    if (util_json_object_get_string (response, "uri", &uri) < 0) {
        errno = EPROTO;
        goto done;
    }
    *urip = xstrdup (uri);
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return ret;
}

/* Emulate former flux_t handle operations.
 */

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg)
{
    char *topic = NULL;
    json_object *payload = NULL;
    int rc = -1;

    if (!*zmsg || cmb_msg_decode (*zmsg, &topic, &payload) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (flux_event_pub (h, topic, payload) < 0)
        goto done;
    if (*zmsg)
        zmsg_destroy (zmsg);
    rc = 0;
done:
    if (topic)
        free (topic);
    if (payload)
        json_object_put (payload);
    return rc;
}

int flux_event_send (flux_t h, json_object *request, const char *fmt, ...)
{
    char *topic;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&topic, fmt, ap) < 0)
        oom ();
    va_end (ap);

    rc = flux_event_pub (h, topic, request);
    free (topic);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
