/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "message.h"
#include "message_private.h"

struct typemap {
    const char *name;
    int type;
};

static struct typemap typemap[] = {
    { ">", FLUX_MSGTYPE_REQUEST },
    { "<", FLUX_MSGTYPE_RESPONSE},
    { "e", FLUX_MSGTYPE_EVENT},
    { "k", FLUX_MSGTYPE_KEEPALIVE},
};
static const int typemap_len = sizeof (typemap) / sizeof (typemap[0]);

static const char *type2prefix (int type)
{
    int i;

    for (i = 0; i < typemap_len; i++)
        if ((type & typemap[i].type))
            return typemap[i].name;
    return "?";
}

struct flagmap {
    const char *name;
    int flag;
};

static struct flagmap flagmap[] = {
    { "topic", FLUX_MSGFLAG_TOPIC},
    { "payload", FLUX_MSGFLAG_PAYLOAD},
    { "noresponse", FLUX_MSGFLAG_NORESPONSE},
    { "route", FLUX_MSGFLAG_ROUTE},
    { "upstream", FLUX_MSGFLAG_UPSTREAM},
    { "private", FLUX_MSGFLAG_PRIVATE},
    { "streaming", FLUX_MSGFLAG_STREAMING},
};
static const int flagmap_len = sizeof (flagmap) / sizeof (flagmap[0]);

static void flags2str (uint8_t flags, char *buf, int buflen)
{
    int i, len = 0;
    buf[0] = '\0';
    for (i = 0; i < flagmap_len; i++) {
        if ((flags & flagmap[i].flag)) {
            if (len) {
                assert (len < (buflen - 1));
                strcat (buf, ",");
                len++;
            }
            assert ((len + strlen (flagmap[i].name)) < (buflen - 1));
            strcat (buf, flagmap[i].name);
            len += strlen (flagmap[i].name);
        }
    }
}

static void userid2str (uint32_t userid, char *buf, int buflen)
{
    int n;
    if (userid == FLUX_USERID_UNKNOWN)
        n = snprintf (buf, buflen, "unknown");
    else
        n = snprintf (buf, buflen, "%u", userid);
    assert (n < buflen);
}

static void rolemask2str (uint32_t rolemask, char *buf, int buflen)
{
    int n;
    switch (rolemask) {
        case FLUX_ROLE_NONE:
            n = snprintf (buf, buflen, "none");
            break;
        case FLUX_ROLE_OWNER:
            n = snprintf (buf, buflen, "owner");
            break;
        case FLUX_ROLE_USER:
            n = snprintf (buf, buflen, "user");
            break;
        case FLUX_ROLE_ALL:
            n = snprintf (buf, buflen, "all");
            break;
        default:
            n = snprintf (buf, buflen, "unknown");
    }
    assert (n < buflen);
}

static void nodeid2str (uint32_t nodeid, char *buf, int buflen)
{
    int n;
    if (nodeid == FLUX_NODEID_ANY)
        n = snprintf (buf, buflen, "any");
    else if (nodeid == FLUX_NODEID_UPSTREAM)
        n = snprintf (buf, buflen, "upstream");
    else
        n = snprintf (buf, buflen, "%u", nodeid);
    assert (n < buflen);
}

void msg_fprint (FILE *f, const flux_msg_t *msg)
{
    int hops;
    const char *prefix;
    char flagsstr[128];
    char useridstr[32];
    char rolemaskstr[32];
    char nodeidstr[32];

    fprintf (f, "--------------------------------------\n");
    if (!msg) {
        fprintf (f, "NULL");
        return;
    }
    prefix = type2prefix (msg->type);
    /* Topic (keepalive has none)
     */
    if (msg->topic)
        fprintf (f, "%s %s\n", prefix, msg->topic);
    /* Proto info
     */
    flags2str (msg->flags, flagsstr, sizeof (flagsstr));
    userid2str (msg->userid, useridstr, sizeof (useridstr));
    rolemask2str (msg->rolemask, rolemaskstr, sizeof (rolemaskstr));
    fprintf (f, "%s flags=%s userid=%s rolemask=%s ",
             prefix, flagsstr, useridstr, rolemaskstr);
    switch (msg->type) {
        case FLUX_MSGTYPE_REQUEST:
            nodeid2str (msg->nodeid, nodeidstr, sizeof (nodeidstr));
            fprintf (f, "nodeid=%s matchtag=%u\n",
                     nodeidstr,
                     msg->matchtag);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            fprintf (f, "errnum=%u matchtag=%u\n",
                     msg->errnum,
                     msg->matchtag);
            break;
        case FLUX_MSGTYPE_EVENT:
            fprintf (f, "sequence=%u\n",
                     msg->sequence);
            break;
        case FLUX_MSGTYPE_KEEPALIVE:
            fprintf (f, "errnum=%u status=%u\n",
                     msg->errnum,
                     msg->status);
            break;
        default:
            fprintf (f, "aux1=0x%X aux2=0x%X\n",
                     msg->aux1,
                     msg->aux2);
            break;
    }
    /* Route stack
     */
    hops = flux_msg_route_count (msg); /* -1 if no route stack */
    if (hops > 0) {
        char *rte = flux_msg_route_string (msg);
        assert (rte != NULL);
        fprintf (f, "%s |%s|\n", prefix, rte);
        free (rte);
    };
    /* Payload
     */
    if (flux_msg_has_payload (msg)) {
        const char *s;
        const void *buf;
        int size;
        if (flux_msg_get_string (msg, &s) == 0)
            fprintf (f, "%s %s\n", prefix, s);
        else if (flux_msg_get_payload (msg, &buf, &size) == 0) {
            /* output at max 80 cols worth of info.  We subtract 2 and
             * set 'max' to 78 b/c of the prefix taking 2 bytes.
             */
            int i, iter, max = 78;
            bool ellipses = false;
            fprintf (f, "%s ", prefix);
            if ((size * 2) > max) {
                /* -3 for ellipses, divide by 2 b/c 2 chars of output
                 * per byte */
                iter = (max - 3) / 2;
                ellipses = true;
            }
            else
                iter = size;
            for (i = 0; i < iter; i++)
                fprintf (f, "%02X", ((uint8_t *)buf)[i]);
            if (ellipses)
                fprintf (f, "...");
            fprintf (f, "\n");
        }
        else
            fprintf (f, "malformed payload\n");
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

