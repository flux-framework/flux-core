/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* A flux messages consist of a list of zeromq frames:
 *
 * [route]
 * [route]
 * [route]
 * ...
 * [route]
 * [route delimiter - empty frame]
 * topic frame
 * [payload frame]
 * PROTO frame
 *
 * See also: RFC 3
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <czmq.h>
#include <jansson.h>

#include "src/common/libutil/aux.h"

#include "message.h"

#define FLUX_MSG_MAGIC 0x33321eee
struct flux_msg {
    int magic;
    zmsg_t *zmsg;
    json_t *json;
    struct aux_item *aux;
};

/* Begin manual codec
 * PROTO consists of 4 byte prelude followed by a fixed length
 * array of u32's in network byte order.
 */
#define PROTO_MAGIC         0x8e
#define PROTO_VERSION       1

#define PROTO_OFF_MAGIC     0 /* 1 byte */
#define PROTO_OFF_VERSION   1 /* 1 byte */
#define PROTO_OFF_TYPE      2 /* 1 byte */
#define PROTO_OFF_FLAGS     3 /* 1 byte */
#define PROTO_OFF_U32_ARRAY 4

#define PROTO_IND_USERID    0
#define PROTO_IND_ROLEMASK  1
#define PROTO_IND_AUX1      2
#define PROTO_IND_AUX2      3

#define PROTO_U32_COUNT     4
#define PROTO_SIZE          4 + (PROTO_U32_COUNT * 4)

/* Helpful aliases */
#define PROTO_IND_NODEID    PROTO_IND_AUX1 // request
#define PROTO_IND_MATCHTAG  PROTO_IND_AUX2 // request, response
#define PROTO_IND_SEQUENCE  PROTO_IND_AUX1 // event
#define PROTO_IND_ERRNUM    PROTO_IND_AUX1 // response, keepalive
#define PROTO_IND_STATUS    PROTO_IND_AUX2 // keepalive

static int proto_set_u32 (uint8_t *data, int len, int index, uint32_t val);

static int proto_set_type (uint8_t *data, int len, int type)
{
    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (proto_set_u32 (data, len, PROTO_IND_NODEID,
                               FLUX_NODEID_ANY) < 0)
                return -1;
            if (proto_set_u32 (data, len, PROTO_IND_MATCHTAG,
                               FLUX_MATCHTAG_NONE) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* N.B. don't clobber matchtag from request on set_type */
            if (proto_set_u32 (data, len, PROTO_IND_ERRNUM, 0) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (proto_set_u32 (data, len, PROTO_IND_SEQUENCE, 0) < 0)
                return -1;
            if (proto_set_u32 (data, len, PROTO_IND_AUX2, 0) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_KEEPALIVE:
            if (proto_set_u32 (data, len, PROTO_IND_STATUS, 0) < 0)
                return -1;
            if (proto_set_u32 (data, len, PROTO_IND_ERRNUM, 0) < 0)
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
static int proto_set_u32 (uint8_t *data, int len, int index, uint32_t val)
{
    uint32_t x = htonl (val);
    int offset = PROTO_OFF_U32_ARRAY + index * 4;

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION
                         || index < 0 || index >= PROTO_U32_COUNT)
        return -1;
    memcpy (&data[offset], &x, sizeof (x));
    return 0;
}
static int proto_get_u32 (uint8_t *data, int len, int index, uint32_t *val)
{
    uint32_t x;
    int offset = PROTO_OFF_U32_ARRAY + index * 4;

    if (len < PROTO_SIZE || data[PROTO_OFF_MAGIC] != PROTO_MAGIC
                         || data[PROTO_OFF_VERSION] != PROTO_VERSION
                         || index < 0 || index >= PROTO_U32_COUNT)
        return -1;
    memcpy (&x, &data[offset], sizeof (x));
    *val = ntohl (x);
    return 0;
}
static void proto_init (uint8_t *data, int len, uint8_t flags)
{
    int n;
    assert (len >= PROTO_SIZE);
    memset (data, 0, len);
    data[PROTO_OFF_MAGIC] = PROTO_MAGIC;
    data[PROTO_OFF_VERSION] = PROTO_VERSION;
    data[PROTO_OFF_FLAGS] = flags;
    n = proto_set_u32 (data, len, PROTO_IND_USERID, FLUX_USERID_UNKNOWN);
    assert (n == 0);
    n = proto_set_u32 (data, len, PROTO_IND_ROLEMASK, FLUX_ROLE_NONE);
    assert (n == 0);
}
/* End manual codec
 */

flux_msg_t *flux_msg_create (int type)
{
    uint8_t proto[PROTO_SIZE];
    flux_msg_t *msg = calloc (1, sizeof (*msg));

    if (!msg) {
        errno = ENOMEM;
        goto error;
    }
    msg->magic = FLUX_MSG_MAGIC;
    proto_init (proto, PROTO_SIZE, 0);
    if (proto_set_type (proto, PROTO_SIZE, type) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg->zmsg = zmsg_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (zmsg_addmem (msg->zmsg, proto, PROTO_SIZE) < 0)
        goto error;
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

void flux_msg_destroy (flux_msg_t *msg)
{
    if (msg) {
        assert (msg->magic == FLUX_MSG_MAGIC);
        int saved_errno = errno;
        json_decref (msg->json);
        zmsg_destroy (&msg->zmsg);
        msg->magic =~ FLUX_MSG_MAGIC;
        aux_destroy (&msg->aux);
        free (msg);
        errno = saved_errno;
    }
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_aux_set (const flux_msg_t *const_msg, const char *name,
                      void *aux, flux_free_f destroy)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&msg->aux, name, aux, destroy);
}

void *flux_msg_aux_get (const flux_msg_t *msg, const char *name)
{
    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (msg->aux, name);
}

size_t flux_msg_encode_size (const flux_msg_t *msg)
{
    zframe_t *zf;
    size_t size = 0;

    zf = zmsg_first (msg->zmsg);
    while (zf) {
        size_t n = zframe_size (zf);
        if (n < 255)
            size += 1;
        else
            size += 1 + 4;
        size += n;
        zf = zmsg_next (msg->zmsg);
    }
    return size;
}


int flux_msg_encode (const flux_msg_t *msg, void *buf, size_t size)
{
    uint8_t *p = buf;
    zframe_t *zf;

    zf = zmsg_first (msg->zmsg);
    while (zf) {
        size_t n = zframe_size (zf);
        if (n < 0xff) {
            if (size - (p - (uint8_t *)buf) < n + 1)
                goto nospace;
            *p++ = (uint8_t)n;
        } else {
            if (size - (p - (uint8_t *)buf) < n + 1 + 4)
                goto nospace;
            *p++ = 0xff;
            *(uint32_t *)p = htonl (n);
            p += 4;
        }
        memcpy (p, zframe_data (zf), n);
        p += n;
        zf = zmsg_next (msg->zmsg);
    }
    return 0;
nospace:
    errno = EINVAL;
    return -1;
}

flux_msg_t *flux_msg_decode (const void *buf, size_t size)
{
    flux_msg_t *msg = calloc (1, sizeof (*msg));
    uint8_t const *p = buf;
    zframe_t *zf;
    int saved_errno;

    if (!msg)
        goto nomem;
    msg->magic = FLUX_MSG_MAGIC;
    if (!(msg->zmsg = zmsg_new ()))
        goto nomem;
    while (p - (uint8_t *)buf < size) {
        size_t n = *p++;
        if (n == 0xff) {
            if (size - (p - (uint8_t *)buf) < 4) {
                saved_errno = EINVAL;
                goto error;
            }
            n = ntohl (*(uint32_t *)p);
            p += 4;
        }
        if (size - (p - (uint8_t *)buf) < n) {
            saved_errno = EINVAL;
            goto error;
        }
        if (!(zf = zframe_new (p, n)))
            goto nomem;
        if (zmsg_append (msg->zmsg, &zf) < 0)
            goto nomem;
        p += n;
    }
    return msg;
nomem:
    saved_errno = EINVAL;
error:
    flux_msg_destroy (msg);
    errno = saved_errno;
    return NULL;
}

int flux_msg_set_type (flux_msg_t *msg, int type)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_type (zframe_data (zf), zframe_size (zf), type) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_type (const flux_msg_t *msg, int *type)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), type) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_flags (flux_msg_t *msg, uint8_t fl)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_flags (const flux_msg_t *msg, uint8_t *fl)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_private (flux_msg_t *msg)
{
    uint8_t flags;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_PRIVATE) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_private (const flux_msg_t *msg)
{
    uint8_t flags;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_PRIVATE) ? true : false;
}


int flux_msg_set_userid (flux_msg_t *msg, uint32_t userid)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_USERID, userid) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_userid (const flux_msg_t *msg, uint32_t *userid)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_USERID, userid) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_ROLEMASK, rolemask) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_ROLEMASK, rolemask) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid, int flags)
{
    zframe_t *zf;
    int type;

    if (flags != 0 && flags != FLUX_MSGFLAG_UPSTREAM)
        goto error;
    if (nodeid == FLUX_NODEID_UPSTREAM) /* should have been resolved earlier */
        goto error;
    if (flags == FLUX_MSGFLAG_UPSTREAM && nodeid == FLUX_NODEID_ANY)
        goto error;
    if (!(zf = zmsg_last (msg->zmsg)))
        goto error;
    if (proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0)
        goto error;
    if (type != FLUX_MSGTYPE_REQUEST)
        goto error;
    if (proto_set_u32 (zframe_data (zf), zframe_size (zf),
                       PROTO_IND_NODEID, nodeid) < 0)
        goto error;
    if (proto_mod_flags (zframe_data (zf), zframe_size (zf), flags, false) < 0)
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeid, int *flags)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;
    uint8_t fl;
    uint32_t nid;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_REQUEST
            || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_NODEID, &nid) < 0
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

int flux_msg_set_errnum (flux_msg_t *msg, int e)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_RESPONSE && type != FLUX_MSGTYPE_KEEPALIVE)
            || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_ERRNUM, e) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_errnum (const flux_msg_t *msg, int *e)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;
    uint32_t xe;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_RESPONSE && type != FLUX_MSGTYPE_KEEPALIVE)
            || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_ERRNUM, &xe) < 0) {
        errno = EPROTO;
        return -1;
    }
    *e = xe;
    return 0;
}

int flux_msg_set_seq (flux_msg_t *msg, uint32_t seq)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_EVENT
            || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_SEQUENCE, seq) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_seq (const flux_msg_t *msg, uint32_t *seq)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_EVENT
            || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_SEQUENCE, seq) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_matchtag (flux_msg_t *msg, uint32_t t)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_REQUEST && type != FLUX_MSGTYPE_RESPONSE)
            || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_MATCHTAG, t) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_matchtag (const flux_msg_t *msg, uint32_t *t)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || (type != FLUX_MSGTYPE_REQUEST && type != FLUX_MSGTYPE_RESPONSE)
            || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_MATCHTAG, t) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_status (flux_msg_t *msg, int s)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_KEEPALIVE
            || proto_set_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_STATUS, s) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_status (const flux_msg_t *msg, int *s)
{
    zframe_t *zf = zmsg_last (msg->zmsg);
    int type;
    uint32_t u;

    if (!zf || proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0
            || type != FLUX_MSGTYPE_KEEPALIVE
            || proto_get_u32 (zframe_data (zf), zframe_size (zf),
                              PROTO_IND_STATUS, &u) < 0) {
        errno = EPROTO;
        return -1;
    }
    *s = u;
    return 0;
}

bool flux_msg_cmp_matchtag (const flux_msg_t *msg, uint32_t matchtag)
{
    uint32_t tag, matchgroup;

    if (flux_msg_get_route_count (msg) > 0)
        return false; /* don't match in foreign matchtag domain */
    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return false;
    matchgroup = matchtag>>FLUX_MATCHTAG_GROUP_SHIFT;
    if (matchgroup > 0 && (tag>>FLUX_MATCHTAG_GROUP_SHIFT) != matchgroup)
        return false;
    if (matchgroup == 0 && tag != matchtag)
        return false;
    return true;
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?'))
        return true;
    return false;
}

bool flux_msg_cmp (const flux_msg_t *msg, struct flux_match match)
{
    if (match.typemask != 0) {
        int type;
        if (flux_msg_get_type (msg, &type) < 0)
            return false;
        if ((type & match.typemask) == 0)
            return false;
    }
    if (match.matchtag != FLUX_MATCHTAG_NONE) {
        if (!flux_msg_cmp_matchtag (msg, match.matchtag))
            return false;
    }
    if (match.topic_glob && strlen (match.topic_glob) > 0
                         && strcmp (match.topic_glob, "*") != 0) {
        const char *topic = NULL;
        if (flux_msg_get_topic (msg, &topic) < 0)
            return false;
        if (isa_glob (match.topic_glob)) {
            if (fnmatch (match.topic_glob, topic, 0) != 0)
                return false;
        } else {
            if (strcmp (match.topic_glob, topic) != 0)
                return false;
        }
    }
    return true;
}

int flux_msg_enable_route (flux_msg_t *msg)
{
    uint8_t flags;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if ((flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    if (zmsg_pushmem (msg->zmsg, NULL, 0) < 0) {
        errno = ENOMEM;
        return -1;
    }
    flags |= FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (msg, flags);
}

int flux_msg_clear_route (flux_msg_t *msg)
{
    uint8_t flags;
    zframe_t *zf;
    int size;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE))
        return 0;
    while ((zf = zmsg_pop (msg->zmsg))) {
        size = zframe_size (zf);
        zframe_destroy (&zf);
        if (size == 0)
            break;
    }
    flags &= ~(uint8_t)FLUX_MSGFLAG_ROUTE;
    return flux_msg_set_flags (msg, flags);
}

int flux_msg_push_route (flux_msg_t *msg, const char *id)
{
    uint8_t flags;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    if (zmsg_pushstr (msg->zmsg, id) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int flux_msg_pop_route (flux_msg_t *msg, char **id)
{
    uint8_t flags;
    zframe_t *zf;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE) || !(zf = zmsg_first (msg->zmsg))) {
        errno = EPROTO;
        return -1;
    }
    if (zframe_size (zf) > 0 && (zf = zmsg_pop (msg->zmsg))) {
        if (id) {
            char *s = zframe_strdup (zf);
            if (!s) {
                zframe_destroy (&zf);
                errno = ENOMEM;
                return -1;
            }
            *id = s;
        }
        zframe_destroy (&zf);
    } else {
        if (id)
            *id = NULL;
    }
    return 0;
}

/* replaces flux_msg_nexthop */
int flux_msg_get_route_last (const flux_msg_t *msg, char **id)
{
    uint8_t flags;
    zframe_t *zf;
    char *s = NULL;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE) || !(zf = zmsg_first (msg->zmsg))) {
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
int flux_msg_get_route_first (const flux_msg_t *msg, char **id)
{
    uint8_t flags;
    zframe_t *zf, *zf_next;
    char *s = NULL;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (msg->zmsg);
    while (zf && zframe_size (zf) > 0) {
        zf_next = zmsg_next (msg->zmsg);
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

int flux_msg_get_route_count (const flux_msg_t *msg)
{
    uint8_t flags;
    zframe_t *zf;
    int count = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (msg->zmsg);
    while (zf && zframe_size (zf) > 0) {
        zf = zmsg_next (msg->zmsg);
        count++;
    }
    return count;
}

/* Get sum of size in bytes of route frames
 */
static int flux_msg_get_route_size (const flux_msg_t *msg)
{
    uint8_t flags;
    zframe_t *zf;
    int size = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (msg->zmsg);
    while (zf && zframe_size (zf) > 0) {
        size += zframe_size (zf);
        zf = zmsg_next (msg->zmsg);
    }
    return size;
}

static zframe_t *flux_msg_get_route_nth (const flux_msg_t *msg, int n)
{
    uint8_t flags;
    zframe_t *zf;
    int count = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return NULL;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return NULL;
    }
    zf = zmsg_first (msg->zmsg);
    while (zf && zframe_size (zf) > 0) {
        if (count == n)
            return zf;
        zf = zmsg_next (msg->zmsg);
        count++;
    }
    errno = ENOENT;
    return NULL;
}

char *flux_msg_get_route_string (const flux_msg_t *msg)
{
    int hops, len;
    int n;
    zframe_t *zf;
    char *buf, *cp;

    if (msg == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if ((hops = flux_msg_get_route_count (msg)) < 0
                    || (len = flux_msg_get_route_size (msg)) < 0) {
        return NULL;
    }
    if (!(cp = buf = malloc (len + hops + 1))) {
        errno = ENOMEM;
        return NULL;
    }
    for (n = hops - 1; n >= 0; n--) {
        if (cp > buf)
            *cp++ = '!';
        if (!(zf = flux_msg_get_route_nth (msg, n))) {
            free (buf);
            return NULL;
        }
        int cpylen = zframe_size (zf);
        if (cpylen == 32) /* abbreviate long UUID */
            cpylen = 5;
        assert (cp - buf + cpylen < len + hops);
        memcpy (cp, zframe_data (zf), cpylen);
        cp += cpylen;
    }
    *cp = '\0';
    return buf;
}

static bool payload_overlap (const void *b, zframe_t *zf)
{
    return ((char *)b >= (char *)zframe_data (zf)
         && (char *)b <  (char *)zframe_data (zf) + zframe_size (zf));
}

int flux_msg_set_payload (flux_msg_t *msg, const void *buf, int size)
{
    zframe_t *zf;
    uint8_t flags;
    int rc = -1;

    if (!msg) {
        errno = EINVAL;
        goto done;
    }
    json_decref (msg->json);            /* invalidate cached json object */
    msg->json = NULL;
    if (flux_msg_get_flags (msg, &flags) < 0)
        goto done;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)) {
        rc = 0;
        goto done;
    }
    zf = zmsg_first (msg->zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (msg->zmsg);      /* skip route frame */
        if (zf)
            zf = zmsg_next (msg->zmsg);      /* skip route delim */
    }
    if ((flags & FLUX_MSGFLAG_TOPIC)) {
        if (zf)
            zf = zmsg_next (msg->zmsg);      /* skip topic frame */
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        goto done;
    }
    /* Case #1: replace existing payload.
     */
    if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        if (zframe_data (zf) != buf || zframe_size (zf) != size) {
            if (payload_overlap (buf, zf)) {
                errno = EINVAL;
                goto done;
            }
            zframe_reset (zf, buf, size);
        }
    /* Case #2: add payload.
     */
    } else if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        zmsg_remove (msg->zmsg, zf);
        if (zmsg_addmem (msg->zmsg, buf, size) < 0
                                        || zmsg_append (msg->zmsg, &zf) < 0) {
            errno = ENOMEM;
            goto done;
        }
        flags |= FLUX_MSGFLAG_PAYLOAD;
    /* Case #3: remove payload.
     */
    } else if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0)) {
        zmsg_remove (msg->zmsg, zf);
        zframe_destroy (&zf);
        flags &= ~(uint8_t)(FLUX_MSGFLAG_PAYLOAD);
    }
    if (flux_msg_set_flags (msg, flags) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int flux_msg_vpack (flux_msg_t *msg, const char *fmt, va_list ap)
{
    char *json_str = NULL;
    json_t *json;
    int saved_errno;

    if (!(json = json_vpack_ex (NULL, 0, fmt, ap)))
        goto error_inval;
    if (!json_is_object (json))
        goto error_inval;
    if (!(json_str = json_dumps (json, JSON_COMPACT)))
        goto error_inval;
    if (flux_msg_set_string (msg, json_str) < 0)
        goto error;
    free (json_str);
    json_decref (json);
    return 0;
error_inval:
    errno = EINVAL;
error:
    saved_errno = errno;
    free (json_str);
    json_decref (json);
    errno = saved_errno;
    return -1;
}

int flux_msg_pack (flux_msg_t *msg, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_msg_vpack (msg, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_msg_get_payload (const flux_msg_t *msg, const void **buf, int *size)
{
    zframe_t *zf;
    uint8_t flags;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD)) {
        errno = EPROTO;
        return -1;
    }
    zf = zmsg_first (msg->zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (msg->zmsg);
        if (zf)
            zf = zmsg_next (msg->zmsg);
    }
    if ((flags & FLUX_MSGFLAG_TOPIC)) {
        if (zf)
            zf = zmsg_next (msg->zmsg);
    }
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    if (buf)
        *buf = zframe_data (zf);
    if (size)
        *size = zframe_size (zf);
    return 0;
}

bool flux_msg_has_payload (const flux_msg_t *msg)
{
    uint8_t flags;
    if (flux_msg_get_flags (msg, &flags) < 0) {
        errno = 0;
        return false;
    }
    return ((flags & FLUX_MSGFLAG_PAYLOAD));
}

int flux_msg_set_string (flux_msg_t *msg, const char *s)
{
    if (s) {
        return flux_msg_set_payload (msg, s, strlen (s) + 1);
    }
    else
        return flux_msg_set_payload (msg, NULL, 0);
}

int flux_msg_get_string (const flux_msg_t *msg, const char **s)
{
    const char *buf;
    int size;
    int rc = -1;

    if (!s) {
        errno = EINVAL;
        goto done;
    }
    if (flux_msg_get_payload (msg, (const void **)&buf, &size) < 0) {
        errno = 0;
        *s = NULL;
    } else {
        if (!buf || size == 0 || buf[size - 1] != '\0') {
            errno = EPROTO;
            goto done;
        }
        *s = buf;
    }
    rc = 0;
done:
    return rc;
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" with parsed json object for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_vunpack (const flux_msg_t *cmsg, const char *fmt, va_list ap)
{
    int rc = -1;
    const char *json_str;
    json_error_t error;
    flux_msg_t *msg = (flux_msg_t *)cmsg;

    if (!msg || !fmt || *fmt == '\0') {
        errno = EINVAL;
        goto done;
    }
    if (!msg->json) {
        if (flux_msg_get_string (msg, &json_str) < 0)
            goto done;
        if (!json_str || !(msg->json = json_loads (json_str, 0, &error))
                      || !json_is_object (msg->json)) {
            errno = EPROTO;
            goto done;
        }
    }
    if (json_vunpack_ex (msg->json, &error, 0, fmt, ap) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_msg_unpack (const flux_msg_t *msg, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = flux_msg_vunpack (msg, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_msg_set_topic (flux_msg_t *msg, const char *topic)
{
    zframe_t *zf, *zf2 = NULL;
    uint8_t flags;
    int rc = -1;

    if (flux_msg_get_flags (msg, &flags) < 0)
        goto done;
    zf = zmsg_first (msg->zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {   /* skip over routing frames, if any */
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (msg->zmsg);
        if (zf)
            zf = zmsg_next (msg->zmsg);
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        goto done;
    }
    if ((flags & FLUX_MSGFLAG_TOPIC) && topic) {        /* case 1: repl topic */
        zframe_reset (zf, topic, strlen (topic) + 1);
    } else if (!(flags & FLUX_MSGFLAG_TOPIC) && topic) {/* case 2: add topic */
        zmsg_remove (msg->zmsg, zf);
        if ((flags & FLUX_MSGFLAG_PAYLOAD) && (zf2 = zmsg_next (msg->zmsg)))
            zmsg_remove (msg->zmsg, zf2);
        if (zmsg_addmem (msg->zmsg, topic, strlen (topic) + 1) < 0
                                    || zmsg_append (msg->zmsg, &zf) < 0
                                    || (zf2 && zmsg_append (msg->zmsg, &zf2) < 0)) {
            errno = ENOMEM;
            goto done;
        }
        flags |= FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            goto done;
    } else if ((flags & FLUX_MSGFLAG_TOPIC) && !topic) { /* case 3: del topic */
        zmsg_remove (msg->zmsg, zf);
        zframe_destroy (&zf);
        flags &= ~(uint8_t)FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

static int zf_topic (const flux_msg_t *msg, zframe_t **zfp)
{
    uint8_t flags;
    zframe_t *zf = NULL;
    int rc = -1;

    if (flux_msg_get_flags (msg, &flags) < 0)
        goto done;
    if (!(flags & FLUX_MSGFLAG_TOPIC)) {
        errno = EPROTO;
        goto done;
    }
    zf = zmsg_first (msg->zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (msg->zmsg);
        if (zf)
            zf = zmsg_next (msg->zmsg);
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

int flux_msg_get_topic (const flux_msg_t *msg, const char **topic)
{
    zframe_t *zf;
    const char *s;
    int rc = -1;

    if (zf_topic (msg, &zf) < 0)
        goto done;
    s = (const char *)zframe_data (zf);
    if (s[zframe_size (zf) - 1] != '\0') {
        errno = EPROTO;
        goto done;
    }
    *topic = s;
    rc = 0;
done:
    return rc;
}

flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload)
{
    flux_msg_t *cpy = NULL;
    zframe_t *zf;
    int count;
    uint8_t flags;
    bool skip_payload = false;

    if (msg->magic != FLUX_MSG_MAGIC) {
        errno = EINVAL;
        goto error;
    }
    /* Set skip_payload = true if caller set 'payload' flag false
     * AND message contains a payload frame.
     */
    if (flux_msg_get_flags (msg, &flags) < 0)
        goto error;
    if (!payload && (flags & FLUX_MSGFLAG_PAYLOAD)) {
        flags &= ~(FLUX_MSGFLAG_PAYLOAD);
        skip_payload = true;
    }
    if (!(cpy = calloc (1, sizeof (*cpy))))
        goto nomem;
    cpy->magic = FLUX_MSG_MAGIC;
    if (!(cpy->zmsg = zmsg_new ()))
        goto nomem;

    /* Copy frames from 'msg' to 'cpy'.
     * 'count' indexes frames from 0 to zmsg_size (msg) - 1.
     * The payload frame (if it exists) will be in the second to last position.
     */
    count = 0;
    zf = zmsg_first (msg->zmsg);
    while (zf) {
        if (!skip_payload || count != zmsg_size (msg->zmsg) - 2) {
            if (zmsg_addmem (cpy->zmsg, zframe_data (zf), zframe_size (zf)) < 0)
                goto nomem;
        }
        zf = zmsg_next (msg->zmsg);
        count++;
    }
    if (flux_msg_set_flags (cpy, flags) < 0)
        goto error;
    return cpy;
nomem:
    errno = ENOMEM;
error:
    flux_msg_destroy (cpy);
    return NULL;
}

struct map_struct {
    const char *name;
    const char *sname;
    int type;
};

static struct map_struct msgtype_map[] = {
    { "request", ">", FLUX_MSGTYPE_REQUEST },
    { "response", "<", FLUX_MSGTYPE_RESPONSE},
    { "event", "e", FLUX_MSGTYPE_EVENT},
    { "keepalive", "k", FLUX_MSGTYPE_KEEPALIVE},
};
static const int msgtype_map_len = 
                            sizeof (msgtype_map) / sizeof (msgtype_map[0]);

const char *flux_msg_typestr (int type)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((type & msgtype_map[i].type))
            return msgtype_map[i].name;
    return "unknown";
}

static const char *msgtype_shortstr (int type)
{
    int i;

    for (i = 0; i < msgtype_map_len; i++)
        if ((type & msgtype_map[i].type))
            return msgtype_map[i].sname;
    return "?";
}

void flux_msg_fprint (FILE *f, const flux_msg_t *msg)
{
    int hops;
    int type = 0;
    zframe_t *proto;
    const char *prefix, *topic = NULL;

    fprintf (f, "--------------------------------------\n");
    if (!msg) {
        fprintf (f, "NULL");
        return;
    }
    if (flux_msg_get_type (msg, &type) < 0
            || (!(proto = zmsg_last (msg->zmsg)))) {
        fprintf (f, "malformed message");
        return;
    }
    prefix = msgtype_shortstr (type);
    (void)flux_msg_get_topic (msg, &topic);
    /* Route stack
     */
    hops = flux_msg_get_route_count (msg); /* -1 if no route stack */
    if (hops >= 0) {
        int len = flux_msg_get_route_size (msg);
        char *rte = flux_msg_get_route_string (msg);
        assert (rte != NULL);
        fprintf (f, "%s[%3.3d] |%s|\n", prefix, len, rte);
        free (rte);
    };
    /* Topic (keepalive has none)
     */
    if (topic)
        fprintf (f, "%s[%3.3zu] %s\n", prefix, strlen (topic), topic);
    /* Payload
     */
    if (flux_msg_has_payload (msg)) {
        const char *s;
        const void *buf;
        int size;
        if (flux_msg_get_string (msg, &s) == 0)
            fprintf (f, "%s[%3.3zu] %s\n", prefix, strlen (s), s);
        else if (flux_msg_get_payload (msg, &buf, &size) == 0)
            fprintf (f, "%s[%3.3d] ...\n", prefix, size);
        else
            fprintf (f, "malformed payload\n");
    }
    /* Proto block
     */
    zframe_fprint (proto, prefix, f);
}

#define IOBUF_MAGIC 0xffee0012

void flux_msg_iobuf_init (struct flux_msg_iobuf *iobuf)
{
    memset (iobuf, 0, sizeof (*iobuf));
}

void flux_msg_iobuf_clean (struct flux_msg_iobuf *iobuf)
{
    if (iobuf->buf && iobuf->buf != iobuf->buf_fixed)
        free (iobuf->buf);
    memset (iobuf, 0, sizeof (*iobuf));
}

int flux_msg_sendfd (int fd, const flux_msg_t *msg,
                     struct flux_msg_iobuf *iobuf)
{
    struct flux_msg_iobuf local;
    struct flux_msg_iobuf *io = iobuf ? iobuf : &local;
    int rc = -1;

    if (fd < 0 || !msg) {
        errno = EINVAL;
        goto done;
    }
    if (!iobuf)
        flux_msg_iobuf_init (&local);
    if (!io->buf) {
        io->size = flux_msg_encode_size (msg) + 8;
        if (io->size <= sizeof (io->buf_fixed))
            io->buf = io->buf_fixed;
        else if (!(io->buf = malloc (io->size))) {
            errno = ENOMEM;
            goto done;
        }
        *(uint32_t *)&io->buf[0] = IOBUF_MAGIC;
        *(uint32_t *)&io->buf[4] = htonl (io->size - 8);
        if (flux_msg_encode (msg, &io->buf[8], io->size - 8) < 0)
            goto done;
        io->done = 0;
    }
    do {
        rc = write (fd, io->buf + io->done, io->size - io->done);
        if (rc < 0)
            goto done;
        io->done += rc;
    } while (io->done < io->size);
    rc = 0;
done:
    if (iobuf) {
        if (rc == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            flux_msg_iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        flux_msg_iobuf_clean (&local);
    }
    return rc;
}

flux_msg_t *flux_msg_recvfd (int fd, struct flux_msg_iobuf *iobuf)
{
    struct flux_msg_iobuf local;
    struct flux_msg_iobuf *io = iobuf ? iobuf : &local;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (fd < 0) {
        errno = EINVAL;
        goto done;
    }
    if (!iobuf)
        flux_msg_iobuf_init (&local);
    if (!io->buf) {
        io->buf = io->buf_fixed;
        io->size = sizeof (io->buf_fixed);
    }
    do {
        if (io->done < 8) {
            rc = read (fd, io->buf + io->done, 8 - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = EPROTO;
                goto done;
            }
            io->done += rc;
            if (io->done == 8) {
                if (*(uint32_t *)&io->buf[0] != IOBUF_MAGIC) {
                    errno = EPROTO;
                    goto done;
                }
                io->size = ntohl (*(uint32_t *)&io->buf[4]) + 8;
                if (io->size > sizeof (io->buf_fixed)) {
                    if (!(io->buf = malloc (io->size))) {
                        errno = EPROTO;
                        goto done;
                    }
                    memcpy (io->buf, io->buf_fixed, 8);
                }
            }
        }
        if (io->done >= 8 && io->done < io->size) {
            rc = read (fd, io->buf + io->done, io->size - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = EPROTO;
                goto done;
            }
            io->done += rc;
        }
    } while (io->done < io->size);
    if (!(msg = flux_msg_decode (io->buf + 8, io->size - 8)))
        goto done;
done:
    if (iobuf) {
        if (msg != NULL || (errno != EAGAIN && errno != EWOULDBLOCK))
            flux_msg_iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        flux_msg_iobuf_clean (&local);
    }
    return msg;
}

int flux_msg_sendzsock (void *sock, const flux_msg_t *msg)
{
    int rc = -1;

    if (!sock || !msg || !zmsg_is (msg->zmsg)) {
        errno = EINVAL;
        goto done;
    }

    void *handle = zsock_resolve (sock);
    int flags = ZFRAME_REUSE | ZFRAME_MORE;
    zframe_t *zf = zmsg_first (msg->zmsg);
    size_t count = 0;

    while (zf) {
        if (++count == zmsg_size (msg->zmsg))
            flags &= ~ZFRAME_MORE;
        if (zframe_send (&zf, handle, flags) < 0)
            goto done;
        zf = zmsg_next (msg->zmsg);
    }
    rc = 0;
done:
    return rc;
}

flux_msg_t *flux_msg_recvzsock (void *sock)
{
    zmsg_t *zmsg;
    flux_msg_t *msg;

    if (!(zmsg = zmsg_recv (sock)))
        return NULL;
    if (!(msg = calloc (1, sizeof (*msg)))) {
        zmsg_destroy (&zmsg);
        errno = ENOMEM;
        return NULL;
    }
    msg->magic = FLUX_MSG_MAGIC;
    msg->zmsg = zmsg;
    return msg;
}

int flux_msg_frames (const flux_msg_t *msg)
{
    return zmsg_size (msg->zmsg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

