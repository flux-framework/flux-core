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

/* zmq.c - wrapper functions for zmq prototyping */

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

#include "zmsg.h"
#include "log.h"
#include "jsonutil.h"
#include "xzmalloc.h"

int zmsg_hopcount (zmsg_t *zmsg)
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

static zframe_t *_tag_frame (zmsg_t *zmsg)
{
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty */
    if (!zf)
        zf = zmsg_first (zmsg); /* rewind - there was no envelope */
    return zf;
}

static zframe_t *_json_frame (zmsg_t *zmsg)
{
    zframe_t *zf = _tag_frame (zmsg);

    return (zf ? zmsg_next (zmsg) : NULL);
}

static zframe_t *_sender_frame (zmsg_t *zmsg)
{
    zframe_t *zf, *prev = NULL;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        prev = zf;
        zf = zmsg_next (zmsg);
    }
    return (zf ? prev : NULL);
}

int cmb_msg_decode (zmsg_t *zmsg, char **tagp, json_object **op)
{
    zframe_t *tag = _tag_frame (zmsg);
    zframe_t *json = zmsg_next (zmsg);

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

zmsg_t *cmb_msg_encode (char *tag, json_object *o)
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
    zframe_t *zf = _tag_frame (zmsg);
    char *p, *ztag;

    if (!zf)
        msg_exit ("_ztag_noaddr: no tag in message");
    if (!(ztag = zframe_strdup (zf)))
        oom ();
    if ((p = strchr (ztag, '!')))
        memmove (ztag, p + 1, strlen (ztag) - (p - ztag)); /* N.B. include \0 */
    return ztag;
}

bool cmb_msg_match (zmsg_t *zmsg, const char *tag)
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

bool cmb_msg_match_substr (zmsg_t *zmsg, const char *tag, char **restp)
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

char *cmb_msg_sender (zmsg_t *zmsg)
{
    zframe_t *zf = _sender_frame (zmsg);
    if (!zf) {
        msg ("%s: empty envelope", __FUNCTION__);
        return NULL;
    }
    return zframe_strdup (zf); /* caller must free */
}

char *cmb_msg_nexthop (zmsg_t *zmsg)
{
    zframe_t *zf = zmsg_first (zmsg);
    if (!zf) {
        msg ("%s: empty envelope", __FUNCTION__);
        return NULL;
    }
    return zframe_strdup (zf); /* caller must free */
}

char *cmb_msg_tag (zmsg_t *zmsg, bool shorten)
{
    zframe_t *zf = _tag_frame (zmsg);
    char *tag;
    if (!zf) {
        msg ("%s: no tag frame", __FUNCTION__);
        return NULL;
    }
    tag = zframe_strdup (zf); /* caller must free */
    if (tag && shorten) {
        char *p = strchr (tag, '.');
        if (p)
            *p = '\0';
    }
    return tag;
}

int cmb_msg_replace_json (zmsg_t *zmsg, json_object *o)
{
    zframe_t *zf = _json_frame (zmsg);
    char *zbuf;
    unsigned int zlen;

    if (!zf) {
        msg ("%s: no JSON frame", __FUNCTION__);
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

int cmb_msg_replace_json_errnum (zmsg_t *zmsg, int errnum)
{
    json_object *no, *o = NULL;

    if (!(o = json_object_new_object ()))
        goto nomem;
    if (!(no = json_object_new_int (errnum)))
        goto nomem;
    json_object_object_add (o, "errnum", no);
    if (cmb_msg_replace_json (zmsg, o) < 0)
        goto error;
    json_object_put (o);
    return 0;
nomem:
    errno = ENOMEM;
error:
    if (o)
        json_object_put (o);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

