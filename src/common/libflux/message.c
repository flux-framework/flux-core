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
#include "src/common/libutil/errno_safe.h"

#include "message.h"

struct flux_msg {
    zmsg_t *zmsg;
    json_t *json;
    char *lasterr;
    struct aux_item *aux;
    int refcount;
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

static flux_msg_t *flux_msg_create_common (void)
{
    flux_msg_t *msg;

    if (!(msg = calloc (1, sizeof (*msg))))
        return NULL;
    msg->refcount = 1;
    return msg;
}

flux_msg_t *flux_msg_create (int type)
{
    uint8_t proto[PROTO_SIZE];
    flux_msg_t *msg;

    if (!(msg = flux_msg_create_common ()))
        return NULL;
    proto_init (proto, PROTO_SIZE, 0);
    if (proto_set_type (proto, PROTO_SIZE, type) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (!(msg->zmsg = zmsg_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (zmsg_addmem (msg->zmsg, proto, PROTO_SIZE) < 0) {
        errno = ENOMEM;
        goto error;
    }
    return msg;
error:
    flux_msg_destroy (msg);
    return NULL;
}

void flux_msg_destroy (flux_msg_t *msg)
{
    if (msg && --msg->refcount == 0) {
        int saved_errno = errno;
        json_decref (msg->json);
        zmsg_destroy (&msg->zmsg);
        aux_destroy (&msg->aux);
        free (msg->lasterr);
        free (msg);
        errno = saved_errno;
    }
}

/* N.B. const attribute of msg argument is defeated internally for
 * incref/decref to allow msg destruction to be juggled to whoever last
 * decrements the reference count.  Other than its eventual destruction,
 * the message content shall not change.
 */
void flux_msg_decref (const flux_msg_t *const_msg)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;
    flux_msg_destroy (msg);
}

const flux_msg_t *flux_msg_incref (const flux_msg_t *const_msg)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;

    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    msg->refcount++;
    return msg;
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

ssize_t flux_msg_encode_size (const flux_msg_t *msg)
{
    zframe_t *zf;
    ssize_t size = 0;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
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

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
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
    flux_msg_t *msg;
    uint8_t const *p = buf;
    zframe_t *zf;

    if (!(msg = flux_msg_create_common ()))
        return NULL;
    if (!(msg->zmsg = zmsg_new ()))
        goto nomem;
    while (p - (uint8_t *)buf < size) {
        size_t n = *p++;
        if (n == 0xff) {
            if (size - (p - (uint8_t *)buf) < 4) {
                errno = EINVAL;
                goto error;
            }
            n = ntohl (*(uint32_t *)p);
            p += 4;
        }
        if (size - (p - (uint8_t *)buf) < n) {
            errno = EINVAL;
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
    errno = ENOMEM;
error:
    flux_msg_destroy (msg);
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
    const uint8_t valid_flags = FLUX_MSGFLAG_TOPIC | FLUX_MSGFLAG_PAYLOAD
                              | FLUX_MSGFLAG_ROUTE | FLUX_MSGFLAG_UPSTREAM
                              | FLUX_MSGFLAG_PRIVATE | FLUX_MSGFLAG_STREAMING
                              | FLUX_MSGFLAG_NORESPONSE;

    if (!msg || fl & ~valid_flags || ((fl & FLUX_MSGFLAG_STREAMING)
                                   && (fl & FLUX_MSGFLAG_NORESPONSE)) != 0) {
        errno = EINVAL;
        return -1;
    }
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_flags (const flux_msg_t *msg, uint8_t *fl)
{
    if (!msg || !fl) {
        errno = EINVAL;
        return -1;
    }
    zframe_t *zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_private (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_PRIVATE) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_private (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_PRIVATE) ? true : false;
}

int flux_msg_set_streaming (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    flags &= ~FLUX_MSGFLAG_NORESPONSE;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_STREAMING) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_streaming (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_STREAMING) ? true : false;
}

int flux_msg_set_noresponse (flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    flags &= ~FLUX_MSGFLAG_STREAMING;
    if (flux_msg_set_flags (msg, flags | FLUX_MSGFLAG_NORESPONSE) < 0)
        return -1;
    return 0;
}

bool flux_msg_is_noresponse (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return true;
    return (flags & FLUX_MSGFLAG_NORESPONSE) ? true : false;
}

int flux_msg_set_userid (flux_msg_t *msg, uint32_t userid)
{
    zframe_t *zf;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_u32 (zframe_data (zf),
                              zframe_size (zf),
                              PROTO_IND_USERID,
                              userid) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_userid (const flux_msg_t *msg, uint32_t *userid)
{
    zframe_t *zf;

    if (!msg || !userid) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_u32 (zframe_data (zf),
                              zframe_size (zf),
                              PROTO_IND_USERID,
                              userid) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask)
{
    zframe_t *zf;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
    if (!zf || proto_set_u32 (zframe_data (zf),
                              zframe_size (zf),
                              PROTO_IND_ROLEMASK,
                              rolemask) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask)
{
    zframe_t *zf;

    if (!msg || !rolemask) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
    if (!zf || proto_get_u32 (zframe_data (zf),
                              zframe_size (zf),
                              PROTO_IND_ROLEMASK,
                              rolemask) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_get_cred (const flux_msg_t *msg, struct flux_msg_cred *cred)
{
    if (!msg || !cred) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_get_rolemask (msg, &cred->rolemask) < 0)
        return -1;
    if (flux_msg_get_userid (msg, &cred->userid) < 0)
        return -1;
    return 0;
}

int flux_msg_set_cred (flux_msg_t *msg, struct flux_msg_cred cred)
{
    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    if (flux_msg_set_rolemask (msg, cred.rolemask) < 0)
        return -1;
    if (flux_msg_set_userid (msg, cred.userid) < 0)
        return -1;
    return 0;
}

int flux_msg_cred_authorize (struct flux_msg_cred cred, uint32_t userid)
{
    if ((cred.rolemask & FLUX_ROLE_OWNER))
        return 0;
    if ((cred.rolemask & FLUX_ROLE_USER) && cred.userid != FLUX_USERID_UNKNOWN
                                         && cred.userid == userid)
        return 0;
    errno = EPERM;
    return -1;
}

int flux_msg_authorize (const flux_msg_t *msg, uint32_t userid)
{
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (msg, &cred) < 0)
        return -1;
    if (flux_msg_cred_authorize (cred, userid) < 0)
        return -1;
    return 0;
}

int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid)
{
    zframe_t *zf;
    int type;

    if (!msg)
        goto error;
    if (nodeid == FLUX_NODEID_UPSTREAM) /* should have been resolved earlier */
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
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeidp)
{
    zframe_t *zf;
    int type;
    uint32_t nodeid;

    if (!msg || !nodeidp) {
        errno = EINVAL;
        return -1;
    }
    if (!(zf = zmsg_last (msg->zmsg)))
        goto error;
    if (proto_get_type (zframe_data (zf), zframe_size (zf), &type) < 0)
        goto error;
    if (type != FLUX_MSGTYPE_REQUEST)
        goto error;
    if (proto_get_u32 (zframe_data (zf), zframe_size (zf),
                       PROTO_IND_NODEID, &nodeid) < 0)
        goto error;
    *nodeidp = nodeid;
    return 0;
error:
    return EPROTO;
    return -1;
}

int flux_msg_set_errnum (flux_msg_t *msg, int e)
{
    zframe_t *zf;;
    int type;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;
    uint32_t xe;

    if (!msg || !e) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;

    if (!msg || !seq) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;

    if (!msg || !t) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    zframe_t *zf;
    int type;
    uint32_t u;

    if (!msg || !s) {
        errno = EINVAL;
        return -1;
    }
    zf = zmsg_last (msg->zmsg);
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
    uint32_t tag;

    if (flux_msg_get_route_count (msg) > 0)
        return false; /* don't match in foreign matchtag domain */
    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return false;
    if (tag != matchtag)
        return false;
    return true;
}

static bool isa_matchany (const char *s)
{
    if (!s || strlen(s) == 0)
        return true;
    if (!strcmp (s, "*"))
        return true;
    return false;
}

static bool isa_glob (const char *s)
{
    if (strchr (s, '*') || strchr (s, '?') || strchr (s, '['))
        return true;
    return false;
}

bool flux_msg_cmp (const flux_msg_t *msg, struct flux_match match)
{
    if (match.typemask != 0) {
        int type = 0;
        if (flux_msg_get_type (msg, &type) < 0)
            return false;
        if ((type & match.typemask) == 0)
            return false;
    }
    if (match.matchtag != FLUX_MATCHTAG_NONE) {
        if (!flux_msg_cmp_matchtag (msg, match.matchtag))
            return false;
    }
    if (!isa_matchany (match.topic_glob)) {
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
    uint8_t flags = 0;

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
    uint8_t flags = 0;
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
    uint8_t flags = 0;

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
    uint8_t flags = 0;
    zframe_t *zf;

    /* do not check 'id' for NULL, a "pop" is acceptable w/o returning
     * data to the user.  Caller may wish to only "pop" and not look
     * at the data.
     */
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
    uint8_t flags = 0;
    zframe_t *zf;
    char *s = NULL;

    if (!id) {
        errno = EINVAL;
        return -1;
    }
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

static zframe_t *find_route_first (const flux_msg_t *msg)
{
    uint8_t flags = 0;
    zframe_t *zf, *zf_next;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return NULL;
    if (!(flags & FLUX_MSGFLAG_ROUTE)) {
        errno = EPROTO;
        return NULL;
    }
    zf = zmsg_first (msg->zmsg);
    while (zf && zframe_size (zf) > 0) {
        zf_next = zmsg_next (msg->zmsg);
        if (zf_next && zframe_size (zf_next) == 0)
            break;
        zf = zf_next;
    }
    return zf;
}

/* replaces flux_msg_sender */
int flux_msg_get_route_first (const flux_msg_t *msg, char **id)
{
    zframe_t *zf;
    char *s = NULL;

    if (!id) {
        errno = EINVAL;
        return -1;
    }
    if (!(zf = find_route_first (msg)))
        return -1;
    if (zframe_size (zf) > 0 && !(s = zframe_strdup (zf))) {
        errno = ENOMEM;
        return -1;
    }
    *id = s;
    return 0;
}

int flux_msg_get_route_count (const flux_msg_t *msg)
{
    uint8_t flags = 0;
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
    uint8_t flags = 0;
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
    uint8_t flags = 0;
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

    if (!msg) {
        errno = EINVAL;
        return NULL;
    }
    if ((hops = flux_msg_get_route_count (msg)) < 0
                    || (len = flux_msg_get_route_size (msg)) < 0) {
        return NULL;
    }
    if (!(cp = buf = malloc (len + hops + 1)))
        return NULL;
    for (n = hops - 1; n >= 0; n--) {
        if (cp > buf)
            *cp++ = '!';
        if (!(zf = flux_msg_get_route_nth (msg, n))) {
            ERRNO_SAFE_WRAP (free, buf);
            return NULL;
        }
        int cpylen = zframe_size (zf);
        if (cpylen > 8) /* abbreviate long UUID */
            cpylen = 8;
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
    uint8_t flags = 0;

    if (!msg) {
        errno = EINVAL;
        return -1;
    }
    json_decref (msg->json);            /* invalidate cached json object */
    msg->json = NULL;
    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_PAYLOAD) && (buf == NULL || size == 0))
        return 0;
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
        return -1;
    }
    /* Case #1: replace existing payload.
     */
    if ((flags & FLUX_MSGFLAG_PAYLOAD) && (buf != NULL && size > 0)) {
        if (zframe_data (zf) != buf || zframe_size (zf) != size) {
            if (payload_overlap (buf, zf)) {
                errno = EINVAL;
                return -1;
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
            return -1;
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
        return -1;
    return 0;
}

static inline void msg_lasterr_reset (flux_msg_t *msg)
{
    if (msg) {
        free (msg->lasterr);
        msg->lasterr = NULL;
    }
}

static inline void msg_lasterr_set (flux_msg_t *msg,
                                    const char *fmt,
                                    ...)
{
    va_list ap;
    int saved_errno = errno;

    va_start (ap, fmt);
    if (vasprintf (&msg->lasterr, fmt, ap) < 0)
        msg->lasterr = NULL;
    va_end (ap);

    errno = saved_errno;
}

int flux_msg_vpack (flux_msg_t *msg, const char *fmt, va_list ap)
{
    char *json_str = NULL;
    json_t *json;
    json_error_t err;
    int saved_errno;

    msg_lasterr_reset (msg);

    if (!(json = json_vpack_ex (&err, 0, fmt, ap))) {
        msg_lasterr_set (msg, "%s", err.text);
        goto error_inval;
    }
    if (!json_is_object (json)) {
        msg_lasterr_set (msg, "payload is not a JSON object");
        goto error_inval;
    }
    if (!(json_str = json_dumps (json, JSON_COMPACT))) {
        msg_lasterr_set (msg, "json_dumps failed on pack result");
        goto error_inval;
    }
    if (flux_msg_set_string (msg, json_str) < 0) {
        msg_lasterr_set (msg, "flux_msg_set_string: %s", strerror (errno));
        goto error;
    }
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
    uint8_t flags = 0;

    if (!buf && !size) {
        errno = EINVAL;
        return -1;
    }
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
    uint8_t flags = 0;
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
    json_error_t err;
    flux_msg_t *msg = (flux_msg_t *)cmsg;

    msg_lasterr_reset (msg);

    if (!msg || !fmt || *fmt == '\0') {
        errno = EINVAL;
        goto done;
    }
    if (!msg->json) {
        if (flux_msg_get_string (msg, &json_str) < 0) {
            msg_lasterr_set (msg, "flux_msg_get_string: %s", strerror (errno));
            goto done;
        }
        if (!json_str) {
            msg_lasterr_set (msg, "message does not have a string payload");
            errno = EPROTO;
            goto done;
        }
        if (!(msg->json = json_loads (json_str, JSON_ALLOW_NUL, &err))) {
            msg_lasterr_set (msg, "%s", err.text);
            errno = EPROTO;
            goto done;
        }
        if (!json_is_object (msg->json)) {
            msg_lasterr_set (msg, "payload is not a JSON object");
            errno = EPROTO;
            goto done;
        }
    }
    if (json_vunpack_ex (msg->json, &err, 0, fmt, ap) < 0) {
        msg_lasterr_set (msg, "%s", err.text);
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

const char *flux_msg_last_error (const flux_msg_t *msg)
{
    if (!msg)
        return "msg object is NULL";
    if (msg->lasterr == NULL)
        return "";
    return msg->lasterr;
}

int flux_msg_set_topic (flux_msg_t *msg, const char *topic)
{
    zframe_t *zf, *zf2 = NULL;
    uint8_t flags = 0;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    zf = zmsg_first (msg->zmsg);
    if ((flags & FLUX_MSGFLAG_ROUTE)) {   /* skip over routing frames, if any */
        while (zf && zframe_size (zf) > 0)
            zf = zmsg_next (msg->zmsg);
        if (zf)
            zf = zmsg_next (msg->zmsg);
    }
    if (!zf) {                          /* must at least have proto frame */
        errno = EPROTO;
        return -1;
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
            return -1;
        }
        flags |= FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            return -1;
    } else if ((flags & FLUX_MSGFLAG_TOPIC) && !topic) { /* case 3: del topic */
        zmsg_remove (msg->zmsg, zf);
        zframe_destroy (&zf);
        flags &= ~(uint8_t)FLUX_MSGFLAG_TOPIC;
        if (flux_msg_set_flags (msg, flags) < 0)
            return -1;
    }
    return 0;
}

static int zf_topic (const flux_msg_t *msg, zframe_t **zfp)
{
    uint8_t flags = 0;
    zframe_t *zf = NULL;

    if (flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    if (!(flags & FLUX_MSGFLAG_TOPIC)) {
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
    if (!zf) {
        errno = EPROTO;
        return -1;
    }
    *zfp = zf;
    return 0;
}

int flux_msg_get_topic (const flux_msg_t *msg, const char **topic)
{
    zframe_t *zf;
    const char *s;

    if (!topic) {
        errno = EINVAL;
        return -1;
    }
    if (zf_topic (msg, &zf) < 0)
        return -1;
    s = (const char *)zframe_data (zf);
    if (s[zframe_size (zf) - 1] != '\0') {
        errno = EPROTO;
        return -1;
    }
    *topic = s;
    return 0;
}

flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload)
{
    flux_msg_t *cpy = NULL;
    zframe_t *zf;
    int count;
    uint8_t flags = 0;
    bool skip_payload = false;

    /* Set skip_payload = true if caller set 'payload' flag false
     * AND message contains a payload frame.
     */
    if (flux_msg_get_flags (msg, &flags) < 0)
        return NULL;
    if (!payload && (flags & FLUX_MSGFLAG_PAYLOAD)) {
        flags &= ~(FLUX_MSGFLAG_PAYLOAD);
        skip_payload = true;
    }
    if (!(cpy = flux_msg_create_common ()))
        return NULL;
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

struct typemap {
    const char *name;
    const char *sname;
    int type;
};

static struct typemap typemap[] = {
    { "request", ">", FLUX_MSGTYPE_REQUEST },
    { "response", "<", FLUX_MSGTYPE_RESPONSE},
    { "event", "e", FLUX_MSGTYPE_EVENT},
    { "keepalive", "k", FLUX_MSGTYPE_KEEPALIVE},
};
static const int typemap_len = sizeof (typemap) / sizeof (typemap[0]);

const char *flux_msg_typestr (int type)
{
    int i;

    for (i = 0; i < typemap_len; i++)
        if ((type & typemap[i].type))
            return typemap[i].name;
    return "unknown";
}

static const char *msgtype_shortstr (int type)
{
    int i;

    for (i = 0; i < typemap_len; i++)
        if ((type & typemap[i].type))
            return typemap[i].sname;
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

int flux_msg_sendzsock_ex (void *sock, const flux_msg_t *msg, bool nonblock)
{
    if (!sock || !msg || !zmsg_is (msg->zmsg)) {
        errno = EINVAL;
        return -1;
    }

    void *handle = zsock_resolve (sock);
    int flags = ZFRAME_REUSE | ZFRAME_MORE;
    zframe_t *zf = zmsg_first (msg->zmsg);
    size_t count = 0;

    if (nonblock)
        flags |= ZFRAME_DONTWAIT;

    while (zf) {
        if (++count == zmsg_size (msg->zmsg))
            flags &= ~ZFRAME_MORE;
        if (zframe_send (&zf, handle, flags) < 0)
            return -1;
        zf = zmsg_next (msg->zmsg);
    }
    return 0;
}

int flux_msg_sendzsock (void *sock, const flux_msg_t *msg)
{
    return flux_msg_sendzsock_ex (sock, msg, false);
}

flux_msg_t *flux_msg_recvzsock (void *sock)
{
    zmsg_t *zmsg;
    flux_msg_t *msg;

    if (!(zmsg = zmsg_recv (sock)))
        return NULL;
    if (!(msg = flux_msg_create_common ())) {
        zmsg_destroy (&zmsg);
        errno = ENOMEM;
        return NULL;
    }
    msg->zmsg = zmsg;
    return msg;
}

int flux_msg_frames (const flux_msg_t *msg)
{
    return zmsg_size (msg->zmsg);
}

struct flux_match flux_match_init (int typemask,
                                     uint32_t matchtag,
                                     const char *topic_glob)
{
    struct flux_match m = {typemask, matchtag, topic_glob};
    return m;
}

void flux_match_free (struct flux_match m)
{
    ERRNO_SAFE_WRAP (free, (char *)m.topic_glob);
}

int flux_match_asprintf (struct flux_match *m, const char *topic_glob_fmt, ...)
{
    va_list args;
    va_start (args, topic_glob_fmt);
    char *topic = NULL;
    int res = vasprintf (&topic, topic_glob_fmt, args);
    va_end (args);
    m->topic_glob = topic;
    return res;
}

bool flux_msg_match_route_first (const flux_msg_t *msg1, const flux_msg_t *msg2)
{
    zframe_t *zf1 = find_route_first (msg1);
    zframe_t *zf2 = find_route_first (msg2);
    int len;

    if (!zf1 || !zf2)
        return false;
    if ((len = zframe_size (zf1)) != zframe_size (zf2)
        || memcmp (zframe_data (zf1), zframe_data (zf2), len) != 0)
        return false;
    return true;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

