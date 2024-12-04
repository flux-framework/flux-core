/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <time.h>
#include <sys/resource.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "method.h"

static char *make_json_response_payload (flux_t *h,
                                         const char *request_payload,
                                         const char *route,
                                         struct flux_msg_cred cred)
{
    uint32_t rank;
    json_t *o = NULL;
    json_t *add = NULL;
    char *result = NULL;

    if (!request_payload || !(o = json_loads (request_payload, 0, NULL))) {
        errno = EPROTO;
        goto done;
    }
    if (flux_get_rank (h, &rank) < 0)
        goto done;
    if (!(add = json_pack ("{s:s s:i s:i s:i}",
                           "route", route,
                           "userid", cred.userid,
                           "rolemask", cred.rolemask,
                           "rank", rank))) {
        errno = ENOMEM;
        goto done;
    }
    if (json_object_update (o, add) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (!(result = json_dumps (o, 0))) {
        errno = ENOMEM;
        goto done;
    }
done:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, add);
    return result;
}

/* The uuid is tacked onto the route string constructed for
 * ping responses.  Truncate the uuid to 8 chars to match policy
 * of flux_msg_route_string().
 */
static const char *get_uuid (flux_t *h)
{
    static char uuid[9] = { 0 };

    if (strlen (uuid) == 0) {
        const char *s = flux_aux_get (h, "flux::uuid");
        if (!s)
            return NULL;
        (void)snprintf (uuid, sizeof (uuid), "%s", s);
    }
    return uuid;
}

void method_ping_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    const char *uuid;
    const char *json_str;
    char *route_str = NULL;
    char *new_str;
    size_t new_size;
    char *resp_str = NULL;
    struct flux_msg_cred cred;

    if (flux_request_decode (msg, NULL, &json_str) < 0
        || flux_msg_get_cred (msg, &cred) < 0
        || !(uuid = get_uuid (h)))
        goto error;

    /* The route string as obtained from the message includes all
     * hops but the last one, e.g. the identity of the destination.
     */
    if (!(route_str = flux_msg_route_string (msg)))
        goto error;
    new_size = strlen (route_str) + strlen (uuid) + 2;
    if (!(new_str = realloc (route_str, new_size)))
        goto error;
    route_str = new_str;
    strcat (route_str, "!");
    strcat (route_str, uuid);

    if (!(resp_str = make_json_response_payload (h, json_str, route_str, cred)))
        goto error;
    if (flux_respond (h, msg, resp_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (route_str);
    free (resp_str);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (route_str);
    free (resp_str);
}

void method_rusage_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct rusage ru;
    const char *s = NULL;
    int who;
    const char *errmsg = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg, NULL, "{s?s}", "who", &s) < 0
        && flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!s || streq (s, "self"))
        who = RUSAGE_SELF;
    else if (streq (s, "children"))
        who = RUSAGE_CHILDREN;
#ifdef RUSAGE_THREAD
    else if (streq (s, "thread"))
        who = RUSAGE_THREAD;
#endif
    else {
        errprintf (&error, "%s is unsupported", s);
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (getrusage (who, &ru) < 0)
        goto error;
    if (flux_respond_pack (h, msg,
            "{s:f s:f s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i}",
            "utime", (double)ru.ru_utime.tv_sec + 1E-6 * ru.ru_utime.tv_usec,
            "stime", (double)ru.ru_stime.tv_sec + 1E-6 * ru.ru_stime.tv_usec,
            "maxrss", ru.ru_maxrss,
            "ixrss", ru.ru_ixrss,
            "idrss", ru.ru_idrss,
            "isrss", ru.ru_isrss,
            "minflt", ru.ru_minflt,
            "majflt", ru.ru_majflt,
            "nswap", ru.ru_nswap,
            "inblock", ru.ru_inblock,
            "oublock", ru.ru_oublock,
            "msgsnd", ru.ru_msgsnd,
            "msgrcv", ru.ru_msgrcv,
            "nsignals", ru.ru_nsignals,
            "nvcsw", ru.ru_nvcsw,
            "nivcsw", ru.ru_nivcsw) < 0)
        flux_log_error (h, "error responding to rusage request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to rusage request");
}

void method_stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    flux_msgcounters_t mcs;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    flux_get_msgcounters (h, &mcs);
    if (flux_respond_pack (h,
                           msg,
                           "{s:{s:i s:i s:i s:i} s:{s:i s:i s:i s:i}}",
                           "tx",
                             "request", mcs.request_tx,
                             "response", mcs.response_tx,
                             "event", mcs.event_tx,
                             "control", mcs.control_tx,
                           "rx",
                             "request", mcs.request_rx,
                             "response", mcs.response_rx,
                             "event", mcs.event_rx,
                             "control", mcs.control_rx) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
}

void method_stats_clear_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    flux_clr_msgcounters (h);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to stats-clear request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-clear request");
}

void method_stats_clear_event_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    if (flux_event_decode (msg, NULL, NULL) == 0)
        flux_clr_msgcounters (h);
}

// vi:ts=4 sw=4 expandtab
