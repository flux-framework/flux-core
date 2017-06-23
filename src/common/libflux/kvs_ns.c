/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>

#include "kvs_ns.h"

/* This key is used to attach re-serialized "object" member of JSON payload
 * to future (lookup_get), or event message (event_decode).
 */
static const char *auxkey = "flux::kvs_ns.json_str";

flux_future_t *flux_kvs_ns_create (flux_t *h, uint32_t nodeid,
                                   const char *name, uint32_t userid,
                                   int flags)
{
    return flux_rpcf (h, "ns.create", nodeid, 0, "{s:s s:i s:i}",
                                                 "name", name,
                                                 "userid", userid,
                                                 "flags", flags);
}

flux_future_t *flux_kvs_ns_remove (flux_t *h, uint32_t nodeid,
                                   const char *name)
{
    return flux_rpcf (h, "ns.remove", nodeid, 0, "{s:s}", "name", name);
}

flux_future_t *flux_kvs_ns_lookup (flux_t *h, uint32_t nodeid,
                                   const char *name, int min_seq, int flags)
{
    return flux_rpcf (h, "ns.lookup", nodeid, 0, "{s:s s:i s:i}",
                                                 "name", name,
                                                 "min_seq", min_seq,
                                                 "flags", flags);
}

int flux_kvs_ns_lookup_get (flux_future_t *f, const char **json_str)
{
    json_t *o;
    char *s;

    if (flux_rpc_getf (f, "{s:o}", "object", &o) < 0)
        return -1;
    if (json_str) {
        if (!(s = flux_future_aux_get (f, auxkey))) {
            if (!(s = json_dumps (o, 0))) {
                errno = EPROTO;
                return -1;
            }
            if (flux_future_aux_set (f, auxkey, s, free) < 0) {
                free (s);
                return -1;
            }
        }
        *json_str = s;
    }
    return 0;
}

int flux_kvs_ns_lookup_getf (flux_future_t *f, const char *fmt, ...)
{
    json_t *o;
    va_list ap;
    int rc;

    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_getf (f, "{s:o}", "object", &o) < 0)
        return -1;
    va_start (ap, fmt);
    rc = json_vunpack_ex (o, NULL, 0, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_kvs_ns_lookup_get_seq (flux_future_t *f, int *seq)
{
    int i;

    if (flux_rpc_getf (f, "{s:i}", "seq", &i) < 0)
        return -1;
    if (seq)
        *seq = i;
    return 0;
}

flux_future_t *flux_kvs_ns_commit (flux_t *h, uint32_t nodeid,
                                   const char *name, int seq,
                                   const char *json_str)
{
    json_t *o;
    flux_future_t *f;

    if (!(o = json_loads (json_str, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    f = flux_rpcf (h, "ns.commit", nodeid, 0, "{s:s s:i s:o}", "name", name,
                                                               "seq", seq,
                                                               "object", o);
    if (!f) {
        json_decref (o);
        return NULL;
    }
    return f;
}

flux_future_t *flux_kvs_ns_commitf (flux_t *h, uint32_t nodeid,
                                    const char *name, int seq,
                                    const char *fmt, ...)
{
    va_list ap;
    json_t *o;
    flux_future_t *f;

    va_start (ap, fmt);
    o = json_vpack_ex (NULL, 0, fmt, ap);
    va_end (ap);
    if (!o) {
        errno = ENOMEM;
        return NULL;
    }
    f = flux_rpcf (h, "ns.commit", nodeid, 0, "{s:s s:i s:o}", "name", name,
                                                               "seq", seq,
                                                               "object", o);
    if (!f) {
        json_decref (o);
        return NULL;
    }
    return f;
}

int flux_kvs_ns_event_decode (const flux_msg_t *msg, const char **namep,
                              int *seqp, const char **json_strp)
{
    const char *topic;
    int seq;
    json_t *object;
    const int prefix_length = strlen ("ns.allcommit.");
    char *str;

    if (flux_event_decodef (msg, &topic, "{s:i s:o}",
                             "seq", &seq,
                             "object", &object) < 0)
        goto error;
    if (strlen (topic) <= prefix_length) {
        errno = EPROTO;
        goto error;
    }
    if (json_strp) {
        if (!(str = flux_msg_aux_get (msg, auxkey))) {
            if (!(str = json_dumps (object, 0))) {
                errno = EPROTO;
                goto error;
            }
            if (flux_msg_aux_set (msg, auxkey, str, free) < 0) {
                free (str);
                goto error;
            }
        }
        *json_strp = str;
    }
    if (namep)
        *namep = topic + prefix_length;
    if (seqp)
        *seqp = seq;
    return 0;
error:
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
