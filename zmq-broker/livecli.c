/* livecli.c - client code for live mdoule */

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
#include "shortjson.h"

int flux_failover (flux_t h, int rank)
{
    JSON response = NULL;
    int rc = -1;

    if ((response = flux_rank_rpc (h, rank, NULL, "live.failover"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (response);
    return rc;
}

int flux_recover (flux_t h, int rank)
{
    JSON response = NULL;
    int rc = -1;

    if ((response = flux_rank_rpc (h, rank, NULL, "live.recover"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (response);
    return rc;
}

int flux_recover_all (flux_t h)
{
    return flux_event_send (h, NULL, "live.recover");
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
