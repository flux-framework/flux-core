/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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

#include "overlay.h"
#include "trace.h"

#include "ccan/str/str.h"

static const char *fake_control_topic (char *buf,
                                       size_t size,
                                       const flux_msg_t *msg)
{
    int ctype;
    int cstatus;

    if (flux_control_decode (msg, &ctype, &cstatus) < 0)
        return NULL;
    snprintf (buf,
              size,
              "%s %d",
              ctype == CONTROL_HEARTBEAT ? "heartbeat" :
              ctype == CONTROL_STATUS ? "status" :
              ctype == CONTROL_DISCONNECT ? "disconnect" : "unknown",
              cstatus);
    return buf;
}

static bool match_module (const char *module_name, json_t *names)
{
    size_t index;
    json_t *entry;

    if (json_array_size (names) == 0)
        return true;
    json_array_foreach (names, index, entry) {
        const char *name = json_string_value (entry);
        if (name && streq (name, module_name))
            return true;
    }
    return false;
}

static bool match_nodeid (uint32_t overlay_peer, int nodeid)
{
    if (overlay_peer == FLUX_NODEID_ANY)
        return true;
    return nodeid == overlay_peer;
}

static void trace_msg (flux_t *h,
                       const char *prefix,
                       uint32_t overlay_peer,      // FLUX_NODEID_ANY if n/a
                       const char *module_name,    // NULL if n/a
                       struct flux_msglist *trace_requests,
                       const flux_msg_t *msg)
{
    const flux_msg_t *req;
    double now;
    int type = 0;
    char buf[64];
    const char *topic = NULL;
    size_t payload_size = 0;
    json_t *payload_json = NULL;
    int errnum = 0;
    const char *errstr = NULL;

    if (!h
        || !prefix
        || !msg
        || !trace_requests
        || flux_msglist_count (trace_requests) == 0)
        return;

    now = flux_reactor_now (flux_get_reactor (h));
    (void)flux_msg_get_type (msg, &type);
    switch (type) {
        case FLUX_MSGTYPE_CONTROL:
            topic = fake_control_topic (buf, sizeof (buf), msg);
            break;
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            (void)flux_msg_get_topic (msg, &topic);
            (void)flux_msg_get_payload (msg, NULL, &payload_size);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            (void)flux_msg_get_topic (msg, &topic);
            if (flux_msg_get_errnum (msg, &errnum) == 0 && errnum > 0)
                (void)flux_msg_get_string (msg, &errstr);
            else
                flux_msg_get_payload (msg, NULL, &payload_size);
            break;
    }
    if (topic && streq (topic, "module.trace")) // avoid getting in a loop!
        return;

    req = flux_msglist_first (trace_requests);
    while (req) {
        struct flux_match match = FLUX_MATCH_ANY;
        int nodeid = -1;
        json_t *names = NULL;
        int full = 0;
        if (flux_request_unpack (req,
                                 NULL,
                                 "{s:i s:s s?i s?o s?b}",
                                 "typemask", &match.typemask,
                                 "topic_glob", &match.topic_glob,
                                 "nodeid", &nodeid,
                                 "names", &names,
                                 "full", &full) < 0
            || (nodeid != -1 && !match_nodeid (overlay_peer, nodeid))
            || (names != NULL && !match_module (module_name, names))
            || !flux_msg_cmp (msg, match))
            goto next;

        if (full && errnum == 0 && !payload_json && payload_size > 0)
            (void)flux_msg_unpack (msg, "o", &payload_json);

        if (flux_respond_pack (h,
                               req,
                               "{s:f s:s s:i s:s? s:i s:s s:i s:O? s:i s:s}",
                               "timestamp", now,
                               "prefix", prefix,
                               "rank", overlay_peer,
                               "name", module_name,
                               "type", type,
                               "topic", topic ? topic : "",
                               "payload_size", payload_size,
                               "payload", full ? payload_json : NULL,
                               "errnum", errnum,
                               "errstr", full && errstr ? errstr : "") < 0)
            flux_log_error (h, "error responding to overlay.trace");
next:
        req = flux_msglist_next (trace_requests);
    }
}

void trace_overlay_msg (flux_t *h,
                        const char *prefix,
                        uint32_t overlay_peer,
                        struct flux_msglist *trace_requests,
                        const flux_msg_t *msg)
{
    trace_msg (h, prefix, overlay_peer, NULL, trace_requests, msg);
}

void trace_module_msg (flux_t *h,
                       const char *prefix,
                       const char *module_name,
                       struct flux_msglist *trace_requests,
                       const flux_msg_t *msg)
{
    trace_msg (h, prefix, FLUX_NODEID_ANY, module_name, trace_requests, msg);
}

// vi:ts=4 sw=4 expandtab
