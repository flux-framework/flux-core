/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <json.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "modservice.h"

/* Route string will not include the endpoints.
 * On arrival here, uuid of dst module has been stripped.
 * The '1' arg to zdump_routestr strips the uuid of the sender.
 * FIXME: t/lua/t0002-rpc.t tries to ping with an array object.
 * This should be caught at a lower level - see issue #181
 */
static int ping_req_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON in = NULL;
    char *s = NULL;

    if (flux_json_request_decode (*zmsg, &in) < 0
         || !json_object_is_type (in, json_type_object)) { // see FIXME above
        flux_err_respond (h, EPROTO, zmsg);
        goto done;
    }
    s = zdump_routestr (*zmsg, 1);
    Jadd_str (in, "route", s);
    if (flux_json_respond (h, in, zmsg) < 0) {
        err ("%s: flux_json_respond", __FUNCTION__);
        goto done;
    }
done:
    Jput (in);
    zmsg_destroy (zmsg);
    if (s)
        free (s);
    return 0;
}

static int stats_get_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    flux_msgcounters_t mcs;
    JSON out = Jnew ();

    flux_get_msgcounters (h, &mcs);
    Jadd_int (out, "#request (tx)", mcs.request_tx);
    Jadd_int (out, "#request (rx)", mcs.request_rx);
    Jadd_int (out, "#response (tx)", mcs.response_tx);
    Jadd_int (out, "#response (rx)", mcs.response_rx);
    Jadd_int (out, "#event (tx)", mcs.event_tx);
    Jadd_int (out, "#event (rx)", mcs.event_rx);
    Jadd_int (out, "#keepalive (tx)", mcs.keepalive_tx);
    Jadd_int (out, "#keepalive (rx)", mcs.keepalive_rx);

    if (flux_json_respond (h, out, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_json_respond: %s", __FUNCTION__,
                  strerror (errno));
    Jput (out);
    zmsg_destroy (zmsg);
    return 0;
}

static int stats_clear_event_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                 void *arg)
{
    flux_clr_msgcounters (h);
    zmsg_destroy (zmsg);
    return 0;
}

static int stats_clear_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                   void *arg)
{
    flux_clr_msgcounters (h);
    if (flux_err_respond (h, 0, zmsg) < 0)
        flux_log (h, LOG_ERR, "%s: flux_err_respond: %s", __FUNCTION__,
                  strerror (errno));
    zmsg_destroy (zmsg);
    return 0;
}

static int rusage_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    JSON out = NULL;
    struct rusage usage;

    if (getrusage (RUSAGE_THREAD, &usage) < 0) {
        if (flux_err_respond (h, errno, zmsg) < 0)
            err ("%s: flux_err_respond", __FUNCTION__);
        goto done;
    }
    out = rusage_to_json (&usage);
    if (flux_json_respond (h, out, zmsg) < 0)
        err ("%s: flux_json_respond", __FUNCTION__);
done:
    Jput (out);
    zmsg_destroy (zmsg);
    return 0;
}

static int shutdown_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    flux_reactor_stop (h);
    zmsg_destroy (zmsg);
    return 0;
}

static void register_event (flux_t h, const char *name,
                            const char *svc, FluxMsgHandler cb)
{
    char *topic = xasprintf ("%s.%s", name, svc);
    if (flux_msghandler_add (h, FLUX_MSGTYPE_EVENT, topic, cb, NULL) < 0)
        err_exit ("%s: flux_msghandler_add", name);
    if (flux_event_subscribe (h, topic) < 0)
        err_exit ("%s: flux_event_subscribe %s", __FUNCTION__, topic);
    free (topic);
}

static void register_request (flux_t h, const char *name,
                              const char *svc, FluxMsgHandler cb)
{
    char *topic = xasprintf ("%s.%s", name, svc);
    if (flux_msghandler_add (h, FLUX_MSGTYPE_REQUEST, topic, cb, NULL) < 0)
        err_exit ("%s: flux_msghandler_add %s", name, topic);
    free (topic);
}

void modservice_register (flux_t h, const char *name)
{
    register_request (h, name, "shutdown", shutdown_cb);
    register_request (h, name, "ping", ping_req_cb);
    register_request (h, name, "stats.get", stats_get_cb);
    register_request (h, name, "stats.clear", stats_clear_request_cb);
    register_request (h, name, "rusage", rusage_cb);

    register_event   (h, name, "stats.clear", stats_clear_event_cb);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
