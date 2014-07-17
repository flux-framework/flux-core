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

char *flux_getattr (flux_t h, int rank, const char *name)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    char *ret = NULL;
    const char *val;

    util_json_object_add_string (request, "name", name);
    if (!(response = flux_rank_rpc (h, rank, request, "cmb.getattr")))
        goto done;
    if (util_json_object_get_string (response, (char *)name, &val) < 0) {
        errno = EPROTO;
        goto done;
    }
    ret = xstrdup (val);
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return ret;
}

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

int flux_size (flux_t h)
{
    int size = -1;
    flux_info (h, NULL, &size, NULL);
    return size;
}

bool flux_treeroot (flux_t h)
{
    bool treeroot = false;
    flux_info (h, NULL, NULL, &treeroot);
    return treeroot;
}

int flux_rmmod (flux_t h, int rank, const char *name, int flags)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rc = -1;

    util_json_object_add_string (request, "name", name);
    util_json_object_add_int (request, "flags", flags);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.rmmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return rc;
}

json_object *flux_lsmod (flux_t h, int rank)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;

    response = flux_rank_rpc (h, rank, request, "cmb.lsmod");
    if (request)
        json_object_put (request);
    return response;
}

int flux_insmod (flux_t h, int rank, const char *path, int flags,
                 json_object *args)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rc = -1;

    util_json_object_add_string (request, "path", path);
    util_json_object_add_int (request, "flags", flags);
    json_object_get_object (args);
    json_object_object_add (request, "args", args);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.insmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return rc;
}

json_object *flux_lspeer (flux_t h, int rank)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;

    response = flux_rank_rpc (h, rank, request, "cmb.lspeer");
    if (request)
        json_object_put (request);
    return response;
}

int flux_reparent (flux_t h, int rank, const char *uri)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rc = -1;

    if (!uri) {
        errno = EINVAL;
        goto done;
    }
    util_json_object_add_string (request, "uri", uri);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.reparent"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return rc;
}

int flux_panic (flux_t h, int rank, const char *msg)
{
    json_object *request = util_json_object_new_object ();
    int rc = -1;

    if (msg)
        util_json_object_add_string (request, "msg", msg);
    if (flux_rank_request_send (h, rank, request, "cmb.panic") < 0)
        goto done;
    /* No reply */
    rc = 0;
done:
    if (request)
        json_object_put (request);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
