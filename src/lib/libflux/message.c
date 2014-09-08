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
#include <zmq.h>
#include <czmq.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <json/json.h>
#include <assert.h>

#include "message.h"
#include "log.h"
#include "jsonutil.h"
#include "xzmalloc.h"

int flux_msg_hopcount (zmsg_t *zmsg)
{
    int count = 0;
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        count++;
        zf = zmsg_next (zmsg); /* skip non-empty */
    }
    if (!zf)
        count = 0;
    return count;
}

static zframe_t *unwrap_zmsg (zmsg_t *zmsg, int frameno)
{
    zframe_t *zf = zmsg_first (zmsg);

    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty routing envelope frames */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty routing envelope delimiter */
    if (!zf)
        zf = zmsg_first (zmsg); /* rewind - there was no routing envelope */
    while (zf && frameno-- > 0) /* frame 0=tag, 1=json */
        zf = zmsg_next (zmsg);
    return zf;
}

static zframe_t *unwrap_zmsg_rte (zmsg_t *zmsg, int hopcount)
{
    int maxhop = flux_msg_hopcount (zmsg);
    zframe_t *zf;

    if (maxhop == 0 || hopcount >= maxhop)
        return NULL;
    if (hopcount == -1) /* -1 means sender frame */
        hopcount = maxhop - 1;
    zf = zmsg_first (zmsg);
    while (zf && hopcount-- > 0)
        zf = zmsg_next (zmsg);
    return zf;
}

int flux_msg_decode (zmsg_t *zmsg, char **tagp, json_object **op)
{
    zframe_t *tag = unwrap_zmsg (zmsg, 0);
    zframe_t *json = unwrap_zmsg (zmsg, 1);

    if (!tag)
        goto eproto;
    if (tagp)
        *tagp = zframe_strdup (tag);
    if (op) {
        if (json)
            util_json_decode (op, (char *)zframe_data (json),
                                          zframe_size (json));
        else
            *op = NULL;
    }
    return 0;
eproto:
    errno = EPROTO;
    return -1;
}

zmsg_t *flux_msg_encode (char *tag, json_object *o)
{
    zmsg_t *zmsg = NULL;
    unsigned int zlen;
    char *zbuf;
    zframe_t *zf;

    if (!(zmsg = zmsg_new ()))
        err_exit ("zmsg_new");
    if (zmsg_addmem (zmsg, tag, strlen (tag)) < 0)
        err_exit ("zmsg_addmem");
    if (o) {
        util_json_encode (o, &zbuf, &zlen);
        if (!(zf = zframe_new (zbuf, zlen)))
            oom ();
        free (zbuf);
        if (zmsg_add (zmsg, zf) < 0)
            oom ();
    }
    return zmsg;
}

static char *_ztag_noaddr (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 0);
    char *p, *ztag;

    if (!zf)
        msg_exit ("_ztag_noaddr: no tag in message");
    if (!(ztag = zframe_strdup (zf)))
        oom ();
    if ((p = strchr (ztag, '!')))
        memmove (ztag, p + 1, strlen (ztag) - (p - ztag)); /* N.B. include \0 */
    return ztag;
}

bool flux_msg_match (zmsg_t *zmsg, const char *tag)
{
    char *ztag = _ztag_noaddr (zmsg);
    const char *tag_noaddr;
    bool match;

    if ((tag_noaddr = strchr (tag, '!')))
        tag_noaddr++;
    else
        tag_noaddr = tag;
    match = !strcmp (ztag, tag_noaddr);
    free (ztag);

    return match;
}

bool flux_msg_match_substr (zmsg_t *zmsg, const char *tag, char **restp)
{
    char *ztag = _ztag_noaddr (zmsg);
    int taglen = strlen (tag);
    int ztaglen = strlen (ztag);

    if (ztaglen >= taglen && strncmp (tag, ztag, taglen) == 0) {
        if (restp) {
            memmove (ztag, ztag + taglen, ztaglen - taglen + 1);
            *restp = ztag;
        } else
            free (ztag);
        return true;
    }
    free (ztag);
    return false;
}

char *flux_msg_sender (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg_rte (zmsg, -1);
    char *s;
    if (!zf) {
        errno = EPROTO;
        return NULL;
    }
    if (!(s = zframe_strdup (zf)))
        oom ();
    return s;
}

char *flux_msg_nexthop (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg_rte (zmsg, 0);
    char *s;
    if (!zf) {
        errno = EPROTO;
        return NULL;
    }
    if (!(s = zframe_strdup (zf)))
        oom ();
    return s;
}

char *flux_msg_tag (zmsg_t *zmsg)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 0);
    char *s;
    if (!zf) {
        errno = EPROTO;
        return NULL;
    }
    if (!(s = zframe_strdup (zf)))
        oom ();
    return s;
}

char *flux_msg_tag_short (zmsg_t *zmsg)
{
    char *p, *s = flux_msg_tag (zmsg);
    if (s && (p = strchr (s, '.')))
        *p = '\0';
    return s;
}

int flux_msg_replace_json (zmsg_t *zmsg, json_object *o)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 1);
    char *zbuf;
    unsigned int zlen;

    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    zmsg_remove (zmsg, zf);
    zframe_destroy (&zf);
    util_json_encode (o, &zbuf, &zlen);
    if (!(zf = zframe_new (zbuf, zlen)))
        oom ();
    free (zbuf);
    if (zmsg_add (zmsg, zf) < 0)
        oom ();
    return 0;
}

int flux_msg_replace_json_errnum (zmsg_t *zmsg, int errnum)
{
    json_object *no, *o = NULL;
    int ret = -1;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_int (errnum)))
        oom ();
    json_object_object_add (o, "errnum", no);
    if (flux_msg_replace_json (zmsg, o) < 0)
        goto done;
    ret = 0;
done:
    json_object_put (o);
    return ret;
}

struct map_struct {
    const char *name;
    const char *sname;
    int typemask;
};

static struct map_struct msgtype_map[] = {
    { "request", ">", FLUX_MSGTYPE_REQUEST },
    { "response", "<", FLUX_MSGTYPE_RESPONSE},
    { "event", "e", FLUX_MSGTYPE_EVENT},
};
static const int msgtype_map_len = 
                            sizeof (msgtype_map) / sizeof (msgtype_map[0]);

const char *flux_msgtype_string (int typemask)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((typemask & msgtype_map[i].typemask))
            return msgtype_map[i].name;
    return "unknown";
}

const char *flux_msgtype_shortstr (int typemask)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((typemask & msgtype_map[i].typemask))
            return msgtype_map[i].sname;
    return "?";
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

