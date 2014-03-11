/* rankcli.c - client code for rank comms module */

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
#include "shortjson.h"
#include "flux.h"

static int flux_rank_sendreq (flux_t h, int rank, const char *topic, J payload)
{
    J request = Jnew ();
    int ret = -1;

    Jadd_int (request, "rank", rank);
    Jadd_str (request, "topic", topic);
    Jadd_obj (request, "payload", payload);
    if (flux_request_send (h, request, "rank.sendreq") < 0)
        goto done;
    ret = 0;
done:
    if (request)
        Jput (request);
    return ret;
}

int flux_rank_request_sendmsg (flux_t h, int rank, zmsg_t **zmsg)
{
    char *topic = NULL;
    J payload = NULL;
    int rc = -1;

    if (!*zmsg || cmb_msg_decode (*zmsg, &topic, &payload) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (flux_rank_sendreq (h, rank, topic, payload) < 0)
        goto done;
    if (*zmsg)
        zmsg_destroy (zmsg);
    rc = 0;
done:
    if (topic)
        free (topic);
    if (payload)
        Jput (payload);
    return rc;
}

int flux_rank_request_send (flux_t h, int rank, J request, const char *fmt, ...)
{
    char *topic;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&topic, fmt, ap) < 0)
        oom ();
    va_end (ap);

    rc = flux_rank_sendreq (h, rank, topic, request);
    free (topic);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
