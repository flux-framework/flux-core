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

static int flux_rank_fwd (flux_t h, int rank, const char *topic, JSON payload)
{
    JSON request = Jnew ();
    int ret = -1;

    Jadd_int (request, "rank", rank);
    Jadd_str (request, "topic", topic);
    Jadd_obj (request, "payload", payload);
    if (flux_request_send (h, request, "rank.fwd") < 0)
        goto done;
    ret = 0;
done:
    Jput (request);
    return ret;
}

int flux_rank_request_sendmsg (flux_t h, int rank, zmsg_t **zmsg)
{
    char *topic = NULL;
    JSON payload = NULL;
    int rc = -1;

    if (rank == -1) {
        rc = flux_request_sendmsg (h, zmsg);
        goto done;
    }

    if (!*zmsg || cmb_msg_decode (*zmsg, &topic, &payload) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (flux_rank_fwd (h, rank, topic, payload) < 0)
        goto done;
    if (*zmsg)
        zmsg_destroy (zmsg);
    rc = 0;
done:
    if (topic)
        free (topic);
    Jput (payload);
    return rc;
}

int flux_rank_request_send (flux_t h, int rank, JSON request,
                            const char *fmt, ...)
{
    char *topic;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&topic, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (rank == -1)
        rc = flux_request_send (h, request, "%s", topic);
    else
        rc = flux_rank_fwd (h, rank, topic, request);
    free (topic);
    return rc;
}

JSON flux_rank_rpc (flux_t h, int rank, JSON request, const char *fmt, ...)
{
    char *tag = NULL;
    JSON response = NULL;
    zmsg_t *zmsg = NULL;
    va_list ap;
    JSON empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = Jnew ();
    zmsg = cmb_msg_encode (tag, request);

    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* add route delimiter */
        err_exit ("zmsg_pushmem");
    if (flux_rank_request_sendmsg (h, rank, &zmsg) < 0)
        goto done;
    if (!(zmsg = flux_response_matched_recvmsg (h, tag, false)))
        goto done;
    if (cmb_msg_decode (zmsg, NULL, &response) < 0 || !response)
        goto done;
    if (Jget_int (response, "errnum", &errno)) {
        Jput (response);
        response = NULL;
        goto done;
    }
done:
    if (tag)
        free (tag);
    if (zmsg)
        zmsg_destroy (&zmsg);
    Jput (empty);
    return response;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
