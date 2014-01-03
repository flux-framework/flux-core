/* barriercli.c - barrier client code */

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

int flux_barrier (flux_t h, const char *name, int nprocs)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    util_json_object_add_string (request, "name", name);
    util_json_object_add_int (request, "count", 1);
    util_json_object_add_int (request, "nprocs", nprocs);

    reply = flux_rpc (h, request, "barrier.enter");
    if (!reply && errno > 0)
        goto done;
    if (reply) {
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
