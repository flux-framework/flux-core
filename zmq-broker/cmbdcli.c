/* cmbdcli.c - client code for built-in cmbd queries */

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

int flux_info (flux_t h, int *rankp, int *sizep, bool *treerootp)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rank, size;
    bool treeroot;
    int ret = -1;

    if (!(response = flux_rpc (h, request, "cmb.info")))
        goto done;
    if (util_json_object_get_boolean (response, "treeroot", &treeroot) < 0
            || util_json_object_get_int (response, "rank", &rank) < 0
            || util_json_object_get_int (response, "size", &size) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (rankp)
        *rankp = rank;
    if (sizep)
        *sizep = size;
    if (treerootp)
        *treerootp = treeroot;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return ret;
}

int flux_route_add (flux_t h, const char *dst, const char *gw)
{
    json_object *request = util_json_object_new_object ();
    int rc = -1;

    util_json_object_add_string (request, "gw", gw);
    if (flux_request_send (h, request, "cmb.route.add.%s", dst) < 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    return rc;
}

int flux_route_del (flux_t h, const char *dst, const char *gw)
{
    json_object *request = util_json_object_new_object ();
    int rc = -1;

    util_json_object_add_string (request, "gw", gw);
    if (flux_request_send (h, request, "cmb.route.del.%s", dst) < 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    return rc;
}

json_object *flux_route_query (flux_t h)
{
    return flux_rpc (h, NULL, "cmb.route.query");
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
