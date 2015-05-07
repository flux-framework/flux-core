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
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fnmatch.h>

#include "message.h"

#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"

/* Begin manual codec
 */
#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1
#define PROTO_SIZE          12
#define PROTO_OFF_MAGIC     0 /* 1 byte */
#define PROTO_OFF_VERSION   1 /* 1 byte */
#define PROTO_OFF_TYPE      2 /* 1 byte */
#define PROTO_OFF_FLAGS     3 /* 1 byte */
#define PROTO_OFF_BIGINT    4 /* 4 bytes */
#define PROTO_OFF_BIGINT2   8 /* 4 bytes */

static int proto_set_bigint (uint8_t *data, int len, uint32_t bigint);
static int proto_set_bigint2 (uint8_t *data, int len, uint32_t bigint);

static int proto_set_type (uint8_t *data, int len, int type)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (proto_set_bigint (data, len, FLUX_NODEID_ANY) < 0)
                return -1;
            if (proto_set_bigint2 (data, len, FLUX_MATCHTAG_NONE) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (proto_set_bigint (data, len, 0) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (proto_set_bigint (data, len, 0) < 0)
                return -1;
            if (proto_set_bigint2 (data, len, 0) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_KEEPALIVE:
            if (proto_set_bigint (data, len, 0) < 0)
                return -1;
            if (proto_set_bigint2 (data, len, 0) < 0)
                return -1;
            break;
        default:
            return -1;
    }
    data[PROTO_OFF_TYPE] = type;
    return 0;
}
static int proto_get_type (uint8_t *data, int len, int *type)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    *type = data[PROTO_OFF_TYPE];
    return 0;
}
static int proto_set_flags (uint8_t *data, int len, uint8_t flags)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    data[PROTO_OFF_FLAGS] = flags;
    return 0;
}
static int proto_get_flags (uint8_t *data, int len, uint8_t *val)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    *val = data[PROTO_OFF_FLAGS];
    return 0;
}
static int proto_mod_flags (uint8_t *data, int len, uint8_t val, bool clear)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    if (clear)
        data[PROTO_OFF_FLAGS] &= ~val;
    else
        data[PROTO_OFF_FLAGS] |= val;
    return 0;
}
static int proto_set_bigint (uint8_t *data, int len, uint32_t bigint)
{
    uint32_t x = htonl (bigint);

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    memcpy (&data[PROTO_OFF_BIGINT], &x, sizeof (x));
    return 0;
}
static int proto_get_bigint (uint8_t *data, int len, uint32_t *bigint)
{
    uint32_t x;

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    memcpy (&x, &data[PROTO_OFF_BIGINT], sizeof (x));
    *bigint = ntohl (x);
    return 0;
}
static int proto_set_bigint2 (uint8_t *data, int len, uint32_t bigint)
{
    uint32_t x = htonl (bigint);

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    memcpy (&data[PROTO_OFF_BIGINT2], &x, sizeof (x));
    return 0;
}
static int proto_get_bigint2 (uint8_t *data, int len, uint32_t *bigint)
{
    uint32_t x;

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    memcpy (&x, &data[PROTO_OFF_BIGINT2], sizeof (x));
    *bigint = ntohl (x);
    return 0;
}
static void proto_init (uint8_t *data, int len, uint8_t flags)
{
    assert (len >= PROTO_SIZE);
    memset (data, 0, len);
    data[PROTO_OFF_MAGIC] = PROTO_MAGIC;
    data[PROTO_OFF_VERSION] = PROTO_VERSION;
    data[PROTO_OFF_FLAGS] = flags;
}
/* End manual codec
 */

zmsg_t *flux_msg_create (int type)
{
    uint8_t proto[PROTO_SIZE];
    zmsg_t *zmsg = NULL;

    proto_init (proto, PROTO_SIZE, 0);
    if (proto_set_type (proto, PROTO_SIZE, type) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!(zmsg = zmsg_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (zmsg_addmem (zmsg, proto, PROTO_SIZE) < 0) {
        zmsg_destroy (&zmsg);
        goto done;
    }
done:
    return zmsg;
}

int flux_msg_set_type (zmsg_t *zmsg, int type)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_set_type (zframe_data (zf), zframe_size (zf), type) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_type (zmsg_t *zmsg, int *type)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), type) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

/* There is no reason to expose flux_msg_set/get_flags()
 * outside of this module for now.
 */

static int flux_msg_set_flags (zmsg_t *zmsg, uint8_t fl)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_set_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int flux_msg_get_flags (zmsg_t *zmsg, uint8_t *fl)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_get_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_nodeid (zmsg_t *zmsg, uint32_t nodeid, int flags)
{
    zframe_t *zf;
    int type;

    if (flags != 0 && flags != FLUX_MSGFLAG_UPSTREAM)
        goto error;
    if (nodeid == FLUX_NODEID_UPSTREAM) /* should have been resolved earlier */
        goto error;
    if (flags == FLUX_MSGFLAG_UPSTREAM && nodeid == FLUX_NODEID_ANY)
        goto error;
    if (!(zf = zmsg_last (zmsg)))
        goto error;
    if (proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0)
        goto error;
    if (type != FLUX_MSGTYPE_REQUEST)
        goto error;
    if (proto_set_bigint (zframe_data (zf), zframe_size (zf), nodeid) < 0)
        goto error;
    if (proto_mod_flags (zframe_data (zf), zframe_size (zf), flags, false) < 0)
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nodeid, int *flags)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;
    uint8_t fl;
    uint32_t nid;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_REQUEST
            || proto_get_bigint (zframe_data (zf), zframe_size (zf), &nid) < 0
            || proto_get_flags (zframe_data (zf), zframe_size (zf), &fl) < 0
            || ((fl & FLUX_MSGFLAG_UPSTREAM) && nid == FLUX_NODEID_ANY)
            || nid == FLUX_NODEID_UPSTREAM) {
        errno = EPROTO;
        return -1;
    }
    *nodeid = nid;
    *flags = (fl & FLUX_MSGFLAG_UPSTREAM);
    return 0;
}

int flux_msg_set_errnum (zmsg_t *zmsg, int e)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_RESPONSE
            || proto_set_bigint (zframe_data (zf), zframe_size (zf), e) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_errnum (zmsg_t *zmsg, int *e)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;
    uint32_t xe;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_RESPONSE
            || proto_get_bigint (zframe_data (zf), zframe_size (zf), &xe) < 0) {
        errno = EPROTO;
        return -1;
    }
    *e = xe;
    return 0;
}

int flux_msg_set_seq (zmsg_t *zmsg, uint32_t seq)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_EVENT
            || proto_set_bigint (zframe_data (zf), zframe_size (zf), seq) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_seq (zmsg_t *zmsg, uint32_t *seq)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_EVENT
            || proto_get_bigint (zframe_data (zf), zframe_size (zf), seq) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_matchtag (zmsg_t *zmsg, uint32_t t)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_REQUEST && type != FLUX_MSGTYPE_RESPONSE)
            || proto_set_bigint2 (zframe_data (zf), zframe_size (zf), t) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_matchtag (zmsg_t *zmsg, uint32_t *t)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_REQUEST && type != FLUX_MSGTYPE_RESPONSE)
            || proto_get_bigint2 (zframe_data (zf), zframe_size (zf), t) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

bool flux_msg_cmp_matchtag (zmsg_t *zmsg, uint32_t matchtag)
{
    uint32_t t;
    if (flux_msg_get_matchtag (zmsg, &t) < 0)
        return false;
    if (t != matchtag)
        return false;
    return true;
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?'))
        return true;
    return false;
}

bool flux_msg_cmp (zmsg_t *zmsg, flux_match_t match)
{
    if (match.typemask != 0) {
        int type;
        if (flux_msg_get_type (zmsg, &type) < 0)
            return false;
        if ((type & match.typemask) == 0)
            return false;
    }
    if (match.matchtag != FLUX_MATCHTAG_NONE) {
        uint32_t matchtag;
        uint32_t lo = match.matchtag;
        uint32_t hi = match.bsize > 1 ? match.matchtag + match.bsize
                                      : match.matchtag;
        if (flux_msg_get_matchtag (zmsg, &matchtag) < 0)
            return false;
        if (matchtag < lo || matchtag > hi)
            return false;
    }
    if (match.topic_glob && strlen (match.topic_glob) > 0
                         && strcmp (match.topic_glob, "*") != 0) {
        if (isa_glob (match.topic_glob)) {
            char *topic = NULL;
            if (flux_msg_get_topic (zmsg, &topic) < 0)
                return false;
            int rc = fnmatch (match.topic_glob, topic, 0);
            free (topic);
            if (rc != 0)
                return false;
        } else {
            if (!flux_msg_streq_topic (zmsg, match.topic_glob))
                return false;
        }
    }
    return true;
}

int flux_msg_enable_route (zmsg_t *zmsg)
{
    uint8_t flags;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if ((flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) {
        errno = ENOMEM;
        return -1;
    }
    flags |= FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (zmsg, flags);
}

int flux_msg_clear_route (zmsg_t *zmsg)
{
    uint8_t flags;
    zframe_t *zf;
    int size;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    while ((zf = zmsg_pop (zmsg))) {
        size = zframe_size (zf);
        zframe_destroy (&zf);
        if (size == 0)
            break;
    }
    flags &= ~(uint8_t)FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (zmsg, flags);
}

int flux_msg_push_route (zmsg_t *zmsg, const char *id)
{
    uint8_t flags;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    if (zmsg_pushstr (zmsg, id) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int flux_msg_pop_route (zmsg_t *zmsg, char **id)
{
    uint8_t flags;
    zframe_t *zf;
    char *s = NULL;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE) || !(zf = zmsg_first (zmsg))) {
        errno = EPROTO;
        return -1;
    }
    if (zframe_size (zf) > 0 && (zf = zmsg_pop (zmsg))
                             && !(s = zframe_strdup (zf))) {
        errno = ENOMEM;
        return -1;
    }
    *id = s;
    return 0;
}

/* replaces flux_msg_nexthop */
int flux_msg_get_route_last (zmsg_t *zmsg, char **id)
{
    uint8_t flags;
    zframe_t *zf;
    char *s = NULL;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE) || !(zf = zmsg_first (zmsg))) {
        errno = EPROTO;
        return -1;
    }
    if (zframe_size (zf) > 0 && !(s = zframe_strdup (zf))) {
        errno = ENOMEM;
        return -1;
    }
    *id = s;
    return 0;
}

/* replaces flux_msg_sender */
int flux_msg_get_route_first (zmsg_t *zmsg, char **id)
{
    uint8_t flags;
    zframe_t *zf, *zf_next;
    char *s = NULL;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) > 0) {
        zf_next = zmsg_next (zmsg);
        if (zf_next && zframe_size (zf_next) == 0)
            break;
        zf = zf_next;
    }
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    if (zframe_size (zf) > 0 && !(s = zframe_strdup (zf))) {
        errno = ENOMEM;
        return -1;
    }
    *id = s;
    return 0;
}

int flux_msg_get_route_count (zmsg_t *zmsg)
{
    uint8_t flags;
    zframe_t *zf;
    int count = 0;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) > 0) {
        zf = zmsg_next (zmsg);
        count++;
    }
    return count;
}

static bool zframe_allocated (void *b, zframe_t *zf)
{
    return ((char *)b >= (char *)zframe_data (zf)
         && (char *)b <  (char *)zframe_data (zf) + zframe_size (zf));
}

int flux_msg_set_payload (zmsg_t *zmsg, int flags, void *buf, int size)
{
    zframe_t *zf;
    uint8_t msgflags;
    int rc = -1;

    if (flags != 0 && flags != FLUX_MSGFLAG_JSON) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_flags (zmsg, &msgflags) < 0)
        goto done;
    if (!(msgflags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)) {
        rc = 0;
        goto done;
    }
    zf = zmsg_first (zmsg);
    if ((msgflags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (zmsg);      /* skip route frame */
        if (zf)
            zf = zmsg_next (zmsg);      /* skip route delim */
    }
    if ((msgflags & FLUX_MSGFLAG_TOPIC)) {
        if (zf)
            zf = zmsg_next (zmsg);      /* skip topic frame */
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        goto done;
    }
    /* Case #1: replace existing payload.
     */
    if ((msgflags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        if (zframe_allocated (buf, zf)) {
            errno = EINVAL;
            goto done;
        }
        zframe_reset (zf, buf, size);
        msgflags &= ~(uint8_t)FLUX_MSGFLAG_JSON;
        msgflags |= flags;
    /* Case #2: add payload.
     */
    } else if (!(msgflags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)){
        zmsg_remove (zmsg, zf);
        if (zmsg_addmem (zmsg, buf, size) < 0 || zmsg_append (zmsg, &zf) < 0) {
            errno = ENOMEM;
            goto done;
        }
        msgflags &= ~(uint8_t)FLUX_MSGFLAG_JSON;
        msgflags |= FLUX_MSGFLAG_PAYLOAD | flags;
    /* Case #3: remove payload.
     */
    } else if ((msgflags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)){
        zmsg_remove (zmsg, zf);
        zframe_destroy (&zf);
        msgflags &= ~(uint8_t)(FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_JSON);
    }
    if (flux_msg_set_flags (zmsg, msgflags) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_msg_get_payload (zmsg_t *zmsg, int *flags, void **buf, int *size)
{
    zframe_t *zf;
    uint8_t msgflags;

    if (flux_msg_get_flags (zmsg, &msgflags) < 0)
        return -1;
    if (!(msgflags & FLUX_MSGFLAG_PAYLOAD)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (zmsg);
    if ((msgflags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (zmsg);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((msgflags & FLUX_MSGFLAG_TOPIC)) {
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    *flags = msgflags & FLUX_MSGFLAG_JSON;
    *buf = zframe_data (zf);
    *size = zframe_size (zf);
    return 0;
}

bool flux_msg_has_payload (zmsg_t *zmsg)
{
    uint8_t flags;
    if (flux_msg_get_flags (zmsg, &flags) < 0) {
        errno = 0;
        return false;
    }
    return ((flags & FLUX_MSGFLAG_PAYLOAD));
}

int flux_msg_set_payload_json (zmsg_t *zmsg, json_object *o)
{
    int rc;
    if (o) {
        const char *s = json_object_to_json_string (o);
        int len = strlen (s);
        rc = flux_msg_set_payload (zmsg, FLUX_MSGFLAG_JSON, (char *)s, len);
    } else
        rc = flux_msg_set_payload (zmsg, 0, NULL, 0);
    return rc;
}

int flux_msg_get_payload_json (zmsg_t *zmsg, json_object **o)
{
    struct json_tokener *tok = NULL;
    int flags;
    char *buf;
    int size;
    int rc = -1;

    if (!o) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_payload (zmsg, &flags, (void **)&buf, &size) < 0) {
        errno = 0;
        *o = NULL;
    } else {
        if (!buf || size == 0 || !(flags & FLUX_MSGFLAG_JSON)) {
            errno = EPROTO;
            goto done;
        }
        if (!(tok = json_tokener_new ())) {
            errno = ENOMEM;
            goto done;
        }
        if (!(*o = json_tokener_parse_ex (tok, buf, size))) {
            errno = EPROTO;
            goto done;
        }
    }
    rc = 0;
done:
    if (tok)
        json_tokener_free (tok);
    return rc;
}

int flux_msg_set_topic (zmsg_t *zmsg, const char *topic)
{
    zframe_t *zf, *zf2 = NULL;
    uint8_t flags;
    int rc = -1;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        goto done;
    zf = zmsg_first (zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {   /* skip over routing frames, if any */
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (zmsg);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        goto done;
    }
    if ((flags & FLUX_MSGFLAG_TOPIC) && topic) {
        zframe_reset (zf, topic, strlen (topic)); /* case 1: replace topic */
    } else if (!(flags & FLUX_MSGFLAG_TOPIC) && topic) {
        zmsg_remove (zmsg, zf);                   /* case 2: add topic */
        if ((flags & FLUX_MSGFLAG_PAYLOAD) && (zf2 = zmsg_next (zmsg)))
            zmsg_remove (zmsg, zf2);
        if (zmsg_addstr (zmsg, topic) < 0 || zmsg_append (zmsg, &zf) < 0
                                    || (zf2 && zmsg_append (zmsg, &zf2) < 0)) {
            errno = ENOMEM;
            goto done;
        }
        flags |= FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (zmsg, flags) < 0)
            goto done;
    } else if ((flags & FLUX_MSGFLAG_TOPIC) && !topic) {
        zmsg_remove (zmsg, zf);                   /* case 3: remove topic */
        flags &= ~(uint8_t)FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (zmsg, flags) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

static int zf_topic (zmsg_t *zmsg, zframe_t **zfp)
{
    uint8_t flags;
    zframe_t *zf = NULL;
    int rc = -1;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        goto done;
    if (!(flags & FLUX_MSGFLAG_TOPIC)) {
        errno = EPROTO;
        goto done;
    }
    zf = zmsg_first (zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (zmsg);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if (!zf) {
        errno = EPROTO;
        goto done;
    }
    *zfp = zf;
    rc = 0;
done:
    return rc;
}

int flux_msg_get_topic (zmsg_t *zmsg, char **topic)
{
    zframe_t *zf;
    int rc = -1;

    if (zf_topic (zmsg, &zf) < 0)
        goto done;
    if (!(*topic = zframe_strdup (zf))) {
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

bool flux_msg_streq_topic (zmsg_t *zmsg, const char *topic)
{
    zframe_t *zf;
    bool same = false;

    if (zf_topic (zmsg, &zf) < 0) {
        errno = 0;
        goto done;
    }
    same = zframe_streq (zf, topic);
done:
    return same;
}

bool flux_msg_strneq_topic (zmsg_t *zmsg, const char *topic, size_t n)
{
    zframe_t *zf = NULL;
    bool same = false;

    if (zf_topic (zmsg, &zf) < 0) {
        errno = 0;
        goto done;
    }
    if (n > zframe_size (zf))
        n = strlen (topic);
    if (n > zframe_size (zf))
        goto done;
    same = !strncmp ((char *)zframe_data (zf), topic, n);
done:
    return same;
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
    { "keepalive", "k", FLUX_MSGTYPE_KEEPALIVE},
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

