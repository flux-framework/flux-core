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

#include "message.h"

#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"

/* Begin manual codec
 *  We have 4 byte values followed by a 32 bit int (network order).
 */
#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1
#define PROTO_SIZE          8
#define PROTO_OFF_MAGIC     0
#define PROTO_OFF_VERSION   1
#define PROTO_OFF_TYPE      2
#define PROTO_OFF_FLAGS     3
#define PROTO_OFF_BIGINT    4
static int proto_set_bigint (uint8_t *data, int len, uint32_t bigint);

static int proto_set_type (uint8_t *data, int len, int type)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_RESPONSE:
        case FLUX_MSGTYPE_EVENT:
        case FLUX_MSGTYPE_KEEPALIVE:
            break;
        default:
            return -1;
    }
    data[PROTO_OFF_TYPE] = type;
    return proto_set_bigint (data, len, type == FLUX_MSGTYPE_REQUEST
                                              ? FLUX_NODEID_ANY : 0);
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

int flux_msg_set_nodeid (zmsg_t *zmsg, uint32_t nid)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_REQUEST
            || proto_set_bigint (zframe_data (zf), zframe_size (zf), nid) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nid)
{
    zframe_t *zf = zmsg_last (zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_REQUEST
            || proto_get_bigint (zframe_data (zf), zframe_size (zf), nid) < 0) {
        errno = EPROTO;
        return -1;
    }
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

int flux_msg_set_payload (zmsg_t *zmsg, void *buf, int size)
{
    zframe_t *zf;
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
    if ((flags & FLUX_MSGFLAG_TOPIC)) { /* skip over topic frame, if any */
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        goto done;
    }
    if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        zframe_reset (zf, buf, size);   /* case 1: replace payload */
    } else if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        zmsg_remove (zmsg, zf);         /* case 2: add payload */
        if (zmsg_addmem (zmsg, buf, size) < 0 || zmsg_append (zmsg, &zf) < 0) {
            errno = ENOMEM;
            goto done;
        }
        flags |= FLUX_MSGFLAG_PAYLOAD;
        if (flux_msg_set_flags (zmsg, flags) < 0)
            goto done;
    } else if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)) {
        zmsg_remove (zmsg, zf);         /* case 3: remove payload */
        zframe_destroy (&zf);
        flags &= ~(uint8_t)FLUX_MSGFLAG_PAYLOAD;
        if (flux_msg_set_flags (zmsg, flags) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_msg_get_payload (zmsg_t *zmsg, void **buf, int *size)
{
    zframe_t *zf;
    uint8_t flags;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (zmsg);
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if ((flags & FLUX_MSGFLAG_TOPIC)) {
        if (zf)
            zf = zmsg_next (zmsg);
    }
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    *buf = zframe_data (zf);
    *size = zframe_size (zf);
    return 0;
}

int flux_msg_set_payload_json (zmsg_t *zmsg, JSON o)
{
    uint8_t flags;
    unsigned int size = 0;
    char *buf = NULL;
    int rc = -1;

    if (o)
        util_json_encode (o, &buf, &size);
    if (flux_msg_set_payload (zmsg, buf, size) < 0)
        goto done;
    if (flux_msg_get_flags (zmsg, &flags) < 0)
        goto done;
    flags |= FLUX_MSGFLAG_JSON;
    if (flux_msg_set_flags (zmsg, flags) < 0)
        goto done;
    rc = 0;
done:
    if (buf)
        free (buf);
    return rc;
}

int flux_msg_get_payload_json (zmsg_t *zmsg, JSON *o)
{
    void *buf;
    int size;
    uint8_t flags;

    if (flux_msg_get_flags (zmsg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_JSON)) {
        errno = EPROTO;
        return -1;
    }
    if (flux_msg_get_payload (zmsg, &buf, &size) < 0)
        return -1;
    util_json_decode (o, buf, size);
    return 0;
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

/**
 ** Deprecated functions
 **/

char *flux_msg_nexthop (zmsg_t *zmsg)
{
    char *id = NULL;
    if (flux_msg_get_route_last (zmsg, &id) < 0) {
        errno = 0;
        return NULL;
    }
    return id;
}

char *flux_msg_sender (zmsg_t *zmsg)
{
    char *id = NULL;
    if (flux_msg_get_route_first (zmsg, &id) < 0) {
        errno = 0;
        return NULL;
    }
    return id;
}

int flux_msg_hopcount (zmsg_t *zmsg)
{
    int n = flux_msg_get_route_count (zmsg);
    if (n < 0) {
        errno = 0;
        return 0;
    }
    return n;
}

int flux_msg_decode (zmsg_t *zmsg, char **topic, json_object **o)
{
    if (topic && flux_msg_get_topic (zmsg, topic) < 0) {
        errno = 0;
        *topic = NULL;
    }
    if (o && flux_msg_get_payload_json (zmsg, o) < 0) {
        errno = 0;
        *o = NULL;
    }
    return 0;
}

char *flux_msg_tag (zmsg_t *zmsg)
{
    char *s;
    if (flux_msg_get_topic (zmsg, &s) < 0) {
        errno = 0;
        return NULL;
    }
    return s;
}

char *flux_msg_tag_short (zmsg_t *zmsg)
{
    char *s, *p;
    if (flux_msg_get_topic (zmsg, &s) < 0) {
        errno = 0;
        return NULL;
    }
    if ((p = strchr (s, '.')))
        *p = '\0';
    return s;
}

int flux_msg_replace_json (zmsg_t *zmsg, json_object *o)
{
    return flux_msg_set_payload_json (zmsg, o);
}

zmsg_t *flux_msg_encode (const char *topic, json_object *o)
{
    zmsg_t *zmsg;

    if (!topic) {
        errno = EINVAL;
        return NULL;
    }
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST))
            || (flux_msg_set_topic (zmsg, topic) < 0)
            || (o && flux_msg_set_payload_json (zmsg, o) < 0)) {
        zmsg_destroy (&zmsg);
        return NULL;
    }
    return zmsg;
}

bool flux_msg_match (zmsg_t *msg, const char *topic)
{
    return flux_msg_streq_topic (msg, topic);
}

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"
#include "src/common/libutil/shortjson.h"

/* flux_msg_encode, flux_msg_decode, flux_msg_match
 *   on message with no JSON frame
 */
void check_legacy_encode (void)
{
    zmsg_t *zmsg;
    JSON o = NULL;
    char *s = NULL;

    errno = 0;
    ok (!(zmsg = flux_msg_encode (NULL, NULL)) && errno == EINVAL,
        "flux_msg_encode with NULL topic fails with errno == EINVAL");
    zmsg_destroy (&zmsg);

    errno = 0;
    ok ((zmsg = flux_msg_encode ("foo", NULL)) && errno == 0
                                               && zmsg_size (zmsg) == 2,
        "flux_msg_encode with NULL json works");

    errno = 0;
    ok ((   !flux_msg_match (zmsg, "f") && errno == 0
            && flux_msg_match (zmsg, "foo") && errno == 0
            && !flux_msg_match (zmsg, "foobar") && errno == 0),
        "flux_msg_match works");

    o = (JSON)&o; // make it non-NULL
    errno = 0;
    ok ((flux_msg_decode (zmsg, &s, &o) == 0 && errno == 0
                                             && s != NULL && o == NULL),
        "flux_msg_decode works");
    like (s, "foo",
        "and returned topic we encoded");
    free (s);
    zmsg_destroy (&zmsg);
}

/* flux_msg_encode, flux_msg_decode, flux_msg_tag, flux_msg_tag_short
 *   on message with JSON
 */
void check_legacy_encode_json (void)
{
    JSON o;
    zmsg_t *zmsg;
    char *s;
    int i;

    o = Jnew ();
    Jadd_int (o, "x", 42);
    errno = 0;
    ok ((zmsg = flux_msg_encode ("a.b.c.d", o)) && errno == 0
                                                && zmsg_size (zmsg) == 3,
        "flux_msg_encode with JSON works");
    Jput (o);

    s = NULL;
    o = NULL;
    errno = 0;
    ok (flux_msg_decode (zmsg, &s, &o) == 0 && errno == 0 && o && s,
        "flux_msg_decode works");
    ok (Jget_int (o, "x", &i) && i == 42,
        "flux_msg_decode returned JSON we encoded");
    Jput (o);
    like (s, "a.b.c.d",
        "flux_msg_decode returned topic string we encoded");
    free (s);

    errno = 0;
    ok ((s = flux_msg_tag (zmsg)) != NULL && errno == 0,
        "flux_msg_tag works");
    like (s, "a.b.c.d",
        "flux_msg_tag returned topic string we encoded");
    free (s);

    errno = 0;
    ok ((s = flux_msg_tag_short (zmsg)) != NULL && errno == 0,
        "flux_msg_tag_short works");
    like (s, "a",
        "flux_msg_tag_short returned first word of topic string");
    free (s);
    zmsg_destroy (&zmsg);
}

/* flux_msg_get_route_first, flux_msg_get_route_last, _get_route_count
 *   on message with variable number of routing frames
 */
void check_routes (void)
{
    zmsg_t *zmsg;
    char *s;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL
        && zmsg_size (zmsg) == 1,
        "flux_msg_create works and creates msg with 1 frame");
    errno = 0;
    ok (flux_msg_get_route_count (zmsg) < 0 && errno == EPROTO,
        "flux_msg_get_route_count returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_first (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_first returns -1 errno EPROTO on msg w/o delim");
    errno = 0;
    ok ((flux_msg_get_route_last (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_get_route_last returns -1 errno EPROTO on msg w/o delim");
    ok ((flux_msg_pop_route (zmsg, &s) == -1 && errno == EPROTO),
        "flux_msg_pop_route returns -1 errno EPROTO on msg w/o delim");

    ok (flux_msg_clear_route (zmsg) == 0 && zmsg_size (zmsg) == 1,
        "flux_msg_clear_route works, is no-op on msg w/o delim");
    ok (flux_msg_enable_route (zmsg) == 0 && zmsg_size (zmsg) == 2,
        "flux_msg_enable_route works, adds one frame on msg w/o delim");
    ok ((flux_msg_get_route_count (zmsg) == 0),
        "flux_msg_get_route_count returns 0 on msg w/delim");
    ok (flux_msg_pop_route (zmsg, &s) == 0 && s == NULL,
        "flux_msg_pop_route works and sets id to NULL on msg w/o routes");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s == NULL,
        "flux_msg_get_route_first returns 0, id=NULL on msg w/delim");
    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s == NULL,
        "flux_msg_get_route_last returns 0, id=NULL on msg w/delim");
    ok (flux_msg_push_route (zmsg, "sender") == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (zmsg) == 1),
        "flux_msg_get_route_count returns 1 on msg w/delim+id");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "sender",
        "flux_msg_get_route_last returns id on msg w/delim+id");
    free (s);

    ok (flux_msg_push_route (zmsg, "router") == 0 && zmsg_size (zmsg) == 4,
        "flux_msg_push_route works and adds a frame");
    ok ((flux_msg_get_route_count (zmsg) == 2),
        "flux_msg_get_route_count returns 2 on msg w/delim+id1+id2");

    ok (flux_msg_get_route_first (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_first works");
    like (s, "sender",
        "flux_msg_get_route_first returns id1 on msg w/delim+id1+id2");
    free (s);

    ok (flux_msg_get_route_last (zmsg, &s) == 0 && s != NULL,
        "flux_msg_get_route_last works");
    like (s, "router",
        "flux_msg_get_route_last returns id2 on message with delim+id1+id2");
    free (s);

    s = NULL;
    ok (flux_msg_pop_route (zmsg, &s) == 0 && s != NULL,
        "flux_msg_pop_route works on msg w/routes");
    like (s, "router",
        "flux_msg_pop_routet returns id2 on message with delim+id1+id2");
    free (s);

    ok (flux_msg_clear_route (zmsg) == 0 && zmsg_size (zmsg) == 1,
        "flux_msg_clear_route strips routing frames and delim");
    zmsg_destroy (&zmsg);
}

/* flux_msg_get_topic, flux_msg_set_topic, flux_msg_streq_topic,
 *  flux_msg_strneq_topic on message with and without routes
 */
void check_topic (void)
{
    zmsg_t *zmsg;
    char *s;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "zmsg_create works");
    errno = 0;
    ok (flux_msg_get_topic (zmsg, &s) < 0 && errno == EPROTO,
       "flux_msg_get_topic fails with EPROTO on msg w/o topic");
    ok (flux_msg_set_topic (zmsg, "blorg") == 0,
       "flux_msg_set_topic works");
    ok (flux_msg_get_topic (zmsg, &s) == 0,
       "flux_msg_get_topic works on msg w/topic");
    like (s, "blorg",
       "and we got back the topic string we set");
    free (s);

    ok (flux_msg_enable_route (zmsg) == 0,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (zmsg, "id1") == 0,
        "flux_msg_push_route works");
    ok (flux_msg_get_topic (zmsg, &s) == 0,
       "flux_msg_get_topic still works, with routes");
    like (s, "blorg",
       "and we got back the topic string we set");
    free (s);

    ok (   !flux_msg_streq_topic (zmsg, "")
        && !flux_msg_streq_topic (zmsg, "bl")
        &&  flux_msg_streq_topic (zmsg, "blorg")
        && !flux_msg_streq_topic (zmsg, "blorgnax"),
        "flux_msg_streq_topic works");
    ok (    flux_msg_strneq_topic (zmsg, "", 0)
        &&  flux_msg_strneq_topic (zmsg, "bl", 2)
        &&  flux_msg_strneq_topic(zmsg, "blorg", 5)
        && !flux_msg_strneq_topic(zmsg, "blorgnax", 8),
        "flux_msg_strneq_topic works");

    zmsg_destroy (&zmsg);
}

/* flux_msg_get_payload, flux_msg_set_payload
 *  on message with and without routes, with and without topic string
 */
void check_payload (void)
{
    zmsg_t *zmsg;
    void *pay[1024], *buf;
    int plen = sizeof (pay), len;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)) != NULL,
       "zmsg_create works");
    errno = 0;
    ok (flux_msg_get_payload (zmsg, &buf, &len) < 0 && errno == EPROTO,
       "flux_msg_get_payload fails with EPROTO on msg w/o topic");
    memset (pay, 42, plen);
    ok (flux_msg_set_payload (zmsg, pay, plen) == 0 && zmsg_size (zmsg) == 2,
       "flux_msg_set_payload works");

    len = 0; buf = NULL;
    ok (flux_msg_get_payload (zmsg, &buf, &len) == 0 && buf && len == plen,
       "flux_msg_get_payload works");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (zmsg, "blorg") == 0 && zmsg_size (zmsg) == 3,
       "flux_msg_set_topic works");
    len = 0; buf = NULL;
    ok (flux_msg_get_payload (zmsg, &buf, &len) == 0 && buf && len == plen,
       "flux_msg_get_payload works with topic");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");
    ok (flux_msg_set_topic (zmsg, NULL) == 0 && zmsg_size (zmsg) == 2,
       "flux_msg_set_topic NULL works");

    ok (flux_msg_enable_route (zmsg) == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_enable_route works");
    ok (flux_msg_push_route (zmsg, "id1") == 0 && zmsg_size (zmsg) == 4,
        "flux_msg_push_route works");

    len = 0; buf = NULL;
    ok (flux_msg_get_payload (zmsg, &buf, &len) == 0 && buf && len == plen,
       "flux_msg_get_payload still works, with routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    ok (flux_msg_set_topic (zmsg, "blorg") == 0 && zmsg_size (zmsg) == 5,
       "flux_msg_set_topic works");
    len = 0; buf = NULL;
    ok (flux_msg_get_payload (zmsg, &buf, &len) == 0 && buf && len == plen,
       "flux_msg_get_payload works, with topic and routes");
    cmp_mem (buf, pay, len,
       "and we got back the payload we set");

    zmsg_destroy (&zmsg);
}

/* flux_msg_replace_json
 *   on message with and without JSON frame
 */
void check_legacy_replace_json (void)
{
    zmsg_t *zmsg;
    JSON o;
    int i;

    ok ((zmsg = flux_msg_encode ("baz", NULL)) != NULL && zmsg_size (zmsg) == 2,
        "flux_msg_encode with topic string works");
    o = Jnew ();
    Jadd_int (o, "x", 2);
    ok (flux_msg_replace_json (zmsg, o) == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_replace_json works on json-less message");
    zmsg_destroy (&zmsg);

    ok ((zmsg = flux_msg_encode ("baz", o)) != NULL && zmsg_size (zmsg) == 3,
        "flux_msg_encode works with topic string and JSON");
    Jput (o);

    o = Jnew ();
    Jadd_int (o, "y", 3);
    ok (flux_msg_replace_json (zmsg, o) == 0 && zmsg_size (zmsg) == 3,
        "flux_msg_replace_json works json-ful message");
    Jput (o);
    ok (flux_msg_decode (zmsg, NULL, &o) == 0 && o != NULL,
        "flux_msg_decode works");
    ok (Jget_int (o, "y", &i) && i == 3,
        "flux_msg_decode returned replaced json");
    Jput (o);

    zmsg_destroy (&zmsg);
}

/* flux_msg_set_type, flux_msg_get_type
 * flux_msg_set_nodeid, flux_msg_get_nodeid
 * flux_msg_set_errnum, flux_msg_get_errnum
 */
void check_proto (void)
{
    zmsg_t *zmsg;
    uint32_t nodeid;
    int errnum;
    int type;

    ok ((zmsg = flux_msg_create (FLUX_MSGTYPE_RESPONSE)) != NULL,
        "flux_msg_create works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");

    ok (flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_REQUEST,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_get_nodeid (zmsg, &nodeid) == 0 && nodeid == FLUX_NODEID_ANY,
        "flux_msg_get_nodeid works on request and default is sane");

    nodeid = 42;
    ok (flux_msg_set_nodeid (zmsg, nodeid) == 0,
        "flux_msg_set_nodeid works on request");
    nodeid = 0;
    ok (flux_msg_get_nodeid (zmsg, &nodeid) == 0 && nodeid == 42,
        "flux_msg_get_nodeid works and returns what we set");

    errno = 0;
    ok (flux_msg_set_errnum (zmsg, 42) < 0 && errno == EINVAL,
        "flux_msg_set_errnum on non-response fails with errno == EINVAL");
    ok (flux_msg_set_type (zmsg, FLUX_MSGTYPE_RESPONSE) == 0,
        "flux_msg_set_type works");
    ok (flux_msg_get_type (zmsg, &type) == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "flux_msg_get_type works and returns what we set");
    ok (flux_msg_set_errnum (zmsg, 43) == 0,
        "flux_msg_set_errnum works on response");
    errno = 0;
    ok (flux_msg_set_nodeid (zmsg, 0) < 0 && errno == EINVAL,
        "flux_msg_set_nodeid on non-request fails with errno == EINVAL");
    errnum = 0;
    ok (flux_msg_get_errnum (zmsg, &errnum) == 0 && errnum == 43,
        "flux_msg_get_errnum works and returns what we set");
    zmsg_destroy (&zmsg);
}

int main (int argc, char *argv[])
{
    plan (86);

    lives_ok ({zmsg_test (false);}, // 1
        "zmsg_test doesn't assert");

    check_proto ();                 // 13
    check_routes ();                // 26
    check_topic ();                 // 11
    check_payload ();               // 16

    check_legacy_encode ();         // 5
    check_legacy_encode_json ();    // 8
    check_legacy_replace_json ();   // 6

    done_testing();
    return (0);
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

