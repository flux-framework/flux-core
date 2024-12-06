/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* A flux message contains route, topic, payload protocol information.
 * When sent it is formed into the following zeromq frames.
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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/aux.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

#include "message_private.h"
#include "message_iovec.h"
#include "message_route.h"
#include "message_proto.h"

static int msg_validate (const flux_msg_t *msg)
{
    if (!msg || msg->refcount <= 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void msg_setup_type (flux_msg_t *msg)
{
    switch (msg_typeof (msg)) {
        case FLUX_MSGTYPE_REQUEST:
            msg->proto.nodeid = FLUX_NODEID_ANY;
            msg->proto.matchtag = FLUX_MATCHTAG_NONE;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* N.B. don't clobber matchtag from request on set_type */
            msg->proto.errnum = 0;
            break;
        case FLUX_MSGTYPE_EVENT:
            msg->proto.sequence = 0;
            msg->proto.aux2 = 0;
            break;
        case FLUX_MSGTYPE_CONTROL:
            msg->proto.control_type = 0;
            msg->proto.control_status = 0;
            break;
    }
}

flux_msg_t *msg_create (void)
{
    flux_msg_t *msg;

    if (!(msg = calloc (1, sizeof (*msg))))
        return NULL;
    list_head_init (&msg->routes);
    list_node_init (&msg->list);
    msg->proto.userid = FLUX_USERID_UNKNOWN;
    msg->proto.rolemask = FLUX_ROLE_NONE;
    msg->refcount = 1;
    return msg;
}

flux_msg_t *flux_msg_create (int type)
{
    flux_msg_t *msg;

    if (!msgtype_is_valid (type)) {
        errno = EINVAL;
        return NULL;
    }
    if (!(msg = msg_create ()))
        return NULL;
    msg->proto.type = type;
    msg_setup_type (msg);
    return msg;
}

void flux_msg_destroy (flux_msg_t *msg)
{
    if (msg && msg->refcount > 0 && --msg->refcount == 0) {
        int saved_errno = errno;
        if (msg_has_route (msg))
            msg_route_clear (msg);
        free (msg->topic);
        free (msg->payload);
        json_decref (msg->json);
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

    if (msg_validate (msg) < 0)
        return NULL;
    msg->refcount++;
    return msg;
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_aux_set (const flux_msg_t *const_msg,
                      const char *name,
                      void *aux,
                      flux_free_f destroy)
{
    flux_msg_t *msg = (flux_msg_t *)const_msg;
    if (msg_validate (msg) < 0)
        return -1;
    return aux_set (&msg->aux, name, aux, destroy);
}

void *flux_msg_aux_get (const flux_msg_t *msg, const char *name)
{
    if (msg_validate (msg) < 0)
        return NULL;
    return aux_get (msg->aux, name);
}

static void encode_count (ssize_t *size, size_t len)
{
    if (len < 255)
        (*size) += 1;
    else
        (*size) += 1 + 4;
    (*size) += len;
}

ssize_t flux_msg_encode_size (const flux_msg_t *msg)
{
    ssize_t size = 0;

    if (msg_validate (msg) < 0)
        return -1;

    encode_count (&size, PROTO_SIZE);
    if (msg_has_payload (msg))
        encode_count (&size, msg->payload_size);
    if (msg_has_topic (msg))
        encode_count (&size, strlen (msg->topic));
    if (msg_has_route (msg)) {
        struct route_id *r = NULL;
        /* route delimiter */
        encode_count (&size, 0);
        list_for_each (&msg->routes, r, route_id_node)
            encode_count (&size, strlen (r->id));
    }
    return size;
}

static ssize_t encode_frame (uint8_t *buf,
                             size_t buf_len,
                             void *frame,
                             size_t frame_size)
{
    ssize_t n = 0;
    if (frame_size < 0xff) {
        if (buf_len < (frame_size + 1)) {
            errno = EINVAL;
            return -1;
        }
        *buf++ = (uint8_t)frame_size;
        n += 1;
    } else {
        if (buf_len < (frame_size + 1 + 4)) {
            errno = EINVAL;
            return -1;
        }
        *buf++ = 0xff;
        *(uint32_t *)buf = htonl (frame_size);
        buf += 4;
        n += 1 + 4;
    }
    if (frame && frame_size)
        memcpy (buf, frame, frame_size);
    return (frame_size + n);
}

int flux_msg_encode (const flux_msg_t *msg, void *buf, size_t size)
{
    uint8_t proto[PROTO_SIZE];
    ssize_t total = 0;
    ssize_t n;

    if (msg_validate (msg) < 0)
        return -1;
    /* msg never completed initial setup */
    if (!msg_type_is_valid (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (msg_has_route (msg)) {
        struct route_id *r = NULL;
        list_for_each (&msg->routes, r, route_id_node) {
            if ((n = encode_frame (buf + total,
                                   size - total,
                                   r->id,
                                   strlen (r->id))) < 0)
                return -1;
            total += n;
        }
        /* route delimiter */
        if ((n = encode_frame (buf + total,
                               size - total,
                               NULL,
                               0)) < 0)
            return -1;
        total += n;
    }
    if (msg_has_topic (msg)) {
        if ((n = encode_frame (buf + total,
                               size - total,
                               msg->topic,
                               strlen (msg->topic))) < 0)
            return -1;
        total += n;
    }
    if (msg_has_payload (msg)) {
        if ((n = encode_frame (buf + total,
                               size - total,
                               msg->payload,
                               msg->payload_size)) < 0)
            return -1;
        total += n;
    }
    if (proto_encode (&msg->proto, proto, PROTO_SIZE) < 0
        || (n = encode_frame (buf + total,
                              size - total,
                              proto,
                              PROTO_SIZE)) < 0)
        return -1;
    total += n;
    return 0;
}

flux_msg_t *flux_msg_decode (const void *buf, size_t size)
{
    flux_msg_t *msg;
    const uint8_t *p = buf;
    struct msg_iovec *iov = NULL;
    int iovlen = 0;
    int iovcnt = 0;

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
        if (iovlen <= iovcnt) {
            struct msg_iovec *tmp;
            iovlen += IOVECINCR;
            if (!(tmp = realloc (iov, sizeof (*iov) * iovlen)))
                goto error;
            iov = tmp;
        }
        iov[iovcnt].data = p;
        iov[iovcnt].size = n;
        iovcnt++;
        p += n;
    }
    if (!(msg = iovec_to_msg (iov, iovcnt)))
        goto error;
    free (iov);
    return msg;
error:
    ERRNO_SAFE_WRAP (free, iov);
    return NULL;
}

int flux_msg_set_type (flux_msg_t *msg, int type)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msgtype_is_valid (type)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.type = type;
    msg_setup_type (msg);
    return 0;
}

int flux_msg_get_type (const flux_msg_t *msg, int *type)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (type)
        *type = msg_typeof (msg);
    return 0;
}

bool flux_msg_has_flag (const flux_msg_t *msg, int flag)
{
    if (msg_validate (msg) < 0)
        return false;
    return msg_has_flag (msg, flag) ? true : false;
}

int flux_msg_set_flag (flux_msg_t *msg, int flag)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msgflags_is_valid (flag)) {
        errno = EINVAL;
        return -1;
    }
    msg_set_flag (msg, flag);
    return 0;
}

int flux_msg_clear_flag (flux_msg_t *msg, int flag)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msgflags_is_valid (flag)) {
        errno = EINVAL;
        return -1;
    }
    msg_clear_flag (msg, flag);
    return 0;
}

int flux_msg_set_private (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return -1;
    msg_set_flag (msg, FLUX_MSGFLAG_PRIVATE);
    return 0;
}

bool flux_msg_is_private (const flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return true;
    return msg_has_private (msg) ? true : false;
}

int flux_msg_set_streaming (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return -1;
    msg_clear_flag (msg, FLUX_MSGFLAG_NORESPONSE);
    msg_set_flag (msg, FLUX_MSGFLAG_STREAMING);
    return 0;
}

bool flux_msg_is_streaming (const flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return true;
    return msg_has_streaming (msg) ? true : false;
}

int flux_msg_set_noresponse (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return -1;
    msg_clear_flag (msg, FLUX_MSGFLAG_STREAMING);
    msg_set_flag (msg, FLUX_MSGFLAG_NORESPONSE);
    return 0;
}

bool flux_msg_is_noresponse (const flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return true;
    return msg_has_noresponse (msg) ? true : false;
}

int flux_msg_set_userid (flux_msg_t *msg, uint32_t userid)
{
    if (msg_validate (msg) < 0)
        return -1;
    msg->proto.userid = userid;
    return 0;
}

int flux_msg_get_userid (const flux_msg_t *msg, uint32_t *userid)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (userid)
        *userid = msg->proto.userid;
    return 0;
}

int flux_msg_set_rolemask (flux_msg_t *msg, uint32_t rolemask)
{
    if (msg_validate (msg) < 0)
        return -1;
    msg->proto.rolemask = rolemask;
    return 0;
}

int flux_msg_get_rolemask (const flux_msg_t *msg, uint32_t *rolemask)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (rolemask)
        *rolemask = msg->proto.rolemask;
    return 0;
}

int flux_msg_get_cred (const flux_msg_t *msg, struct flux_msg_cred *cred)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!cred) {
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
    if (msg_validate (msg) < 0)
        return -1;
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
    if ((cred.rolemask & FLUX_ROLE_USER)
        && cred.userid != FLUX_USERID_UNKNOWN
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

bool flux_msg_is_local (const flux_msg_t *msg)
{
    uint32_t rolemask;
    if (flux_msg_get_rolemask (msg, &rolemask) == 0
        && (rolemask & FLUX_ROLE_LOCAL))
        return true;
    return false;
}

int flux_msg_set_nodeid (flux_msg_t *msg, uint32_t nodeid)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (nodeid == FLUX_NODEID_UPSTREAM) /* should have been resolved earlier */
        goto error;
    if (!msg_is_request (msg))
        goto error;
    msg->proto.nodeid = nodeid;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_msg_get_nodeid (const flux_msg_t *msg, uint32_t *nodeid)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_request (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (nodeid)
        *nodeid = msg->proto.nodeid;
    return 0;
}

int flux_msg_set_errnum (flux_msg_t *msg, int errnum)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_response (msg)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.errnum = errnum;
    return 0;
}

int flux_msg_get_errnum (const flux_msg_t *msg, int *errnum)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_response (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (errnum)
        *errnum = msg->proto.errnum;
    return 0;
}

int flux_msg_set_seq (flux_msg_t *msg, uint32_t seq)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_event (msg)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.sequence = seq;
    return 0;
}

int flux_msg_get_seq (const flux_msg_t *msg, uint32_t *seq)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!(msg_is_event (msg))) {
        errno = EPROTO;
        return -1;
    }
    if (seq)
        *seq = msg->proto.sequence;
    return 0;
}

int flux_msg_set_matchtag (flux_msg_t *msg, uint32_t matchtag)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_request (msg) && !msg_is_response (msg)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.matchtag = matchtag;
    return 0;
}

int flux_msg_get_matchtag (const flux_msg_t *msg, uint32_t *matchtag)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_request (msg) && !msg_is_response (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (matchtag)
        *matchtag = msg->proto.matchtag;
    return 0;
}

int flux_msg_set_control (flux_msg_t *msg, int type, int status)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_control (msg)) {
        errno = EINVAL;
        return -1;
    }
    msg->proto.control_type = type;
    msg->proto.control_status = status;
    return 0;
}

int flux_msg_get_control (const flux_msg_t *msg, int *type, int *status)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_is_control (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (type)
        *type = msg->proto.control_type;
    if (status)
        *status = msg->proto.control_status;
    return 0;
}

bool flux_msg_cmp_matchtag (const flux_msg_t *msg, uint32_t matchtag)
{
    uint32_t tag;

    if (flux_msg_route_count (msg) > 0)
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
    if (streq (s, "*"))
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
            if (!streq (match.topic_glob, topic))
                return false;
        }
    }
    return true;
}

void flux_msg_route_enable (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0 || msg_has_route (msg))
        return;
    msg_set_flag (msg, FLUX_MSGFLAG_ROUTE);
}

void flux_msg_route_disable (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0 || !msg_has_route (msg))
        return;
    msg_route_clear (msg);
    msg_clear_flag (msg, FLUX_MSGFLAG_ROUTE);
}

void flux_msg_route_clear (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0 || !msg_has_route (msg))
        return;
    msg_route_clear (msg);
}

int flux_msg_route_push (flux_msg_t *msg, const char *id)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!id) {
        errno = EINVAL;
        return -1;
    }
    if (!msg_has_route (msg)) {
        errno = EPROTO;
        return -1;
    }
    return msg_route_push (msg, id, strlen (id));
}

int flux_msg_route_delete_last (flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_has_route (msg)) {
        errno = EPROTO;
        return -1;
    }
    return msg_route_delete_last (msg);
}

/* replaces flux_msg_nexthop */
const char *flux_msg_route_last (const flux_msg_t *msg)
{
    struct route_id *r;

    if (msg_validate (msg) < 0 || !msg_has_route (msg))
        return NULL;
    if ((r = list_top (&msg->routes, struct route_id, route_id_node)))
        return r->id;
    return NULL;
}

/* replaces flux_msg_sender */
const char *flux_msg_route_first (const flux_msg_t *msg)
{
    struct route_id *r;

    if (msg_validate (msg) < 0 || !msg_has_route (msg))
        return NULL;
    if ((r = list_tail (&msg->routes, struct route_id, route_id_node)))
        return r->id;
    return NULL;
}

int flux_msg_route_count (const flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_has_route (msg)) {
        errno = EPROTO;
        return -1;
    }
    return msg->routes_len;
}

/* Get sum of size in bytes of route frames
 */
static int flux_msg_route_size (const flux_msg_t *msg)
{
    struct route_id *r = NULL;
    int size = 0;

    assert (msg);
    if (!msg_has_route (msg)) {
        errno = EPROTO;
        return -1;
    }
    list_for_each (&msg->routes, r, route_id_node)
        size += strlen (r->id);
    return size;
}

char *flux_msg_route_string (const flux_msg_t *msg)
{
    struct route_id *r = NULL;
    int hops, len;
    char *buf, *cp;

    if (msg_validate (msg) < 0)
        return NULL;
    if (!msg_has_route (msg)) {
        errno = EPROTO;
        return NULL;
    }
    if ((hops = flux_msg_route_count (msg)) < 0
        || (len = flux_msg_route_size (msg)) < 0)
        return NULL;
    if (!(cp = buf = malloc (len + hops + 1)))
        return NULL;
    list_for_each_rev (&msg->routes, r, route_id_node) {
        if (cp > buf)
            *cp++ = '!';
        int cpylen = strlen (r->id);
        if (cpylen > 8) /* abbreviate long UUID */
            cpylen = 8;
        assert (cp - buf + cpylen < len + hops);
        memcpy (cp, r->id, cpylen);
        cp += cpylen;
    }
    *cp = '\0';
    return buf;
}

static bool payload_overlap (flux_msg_t *msg, const void *b)
{
    return ((char *)b >= (char *)msg->payload
         && (char *)b <  (char *)msg->payload + msg->payload_size);
}

int flux_msg_set_payload (flux_msg_t *msg, const void *buf, size_t size)
{
    if (msg_validate (msg) < 0)
        return -1;
    json_decref (msg->json);            /* invalidate cached json object */
    msg->json = NULL;
    if (!msg_has_payload (msg) && (buf == NULL || size == 0))
        return 0;
    /* Case #1: replace existing payload.
     */
    if (msg_has_payload (msg) && (buf != NULL && size > 0)) {
        assert (msg->payload);
        if (msg->payload != buf || msg->payload_size != size) {
            if (payload_overlap (msg, buf)) {
                errno = EINVAL;
                return -1;
            }
        }
        if (size > msg->payload_size) {
            void *ptr;
            if (!(ptr = realloc (msg->payload, size))) {
                errno = ENOMEM;
                return -1;
            }
            msg->payload = ptr;
            msg->payload_size = size;
        }
        memcpy (msg->payload, buf, size);
    /* Case #2: add payload.
     */
    } else if (!msg_has_payload (msg) && (buf != NULL && size > 0)) {
        assert (!msg->payload);
        if (!(msg->payload = malloc (size)))
            return -1;
        msg->payload_size = size;
        memcpy (msg->payload, buf, size);
        msg_set_flag (msg, FLUX_MSGFLAG_PAYLOAD);
    /* Case #3: remove payload.
     */
    } else if (msg_has_payload (msg) && (buf == NULL || size == 0)) {
        assert (msg->payload);
        free (msg->payload);
        msg->payload = NULL;
        msg->payload_size = 0;
        msg_clear_flag (msg, FLUX_MSGFLAG_PAYLOAD);
    }
    return 0;
}

static inline void msg_lasterr_reset (flux_msg_t *msg)
{
    if (msg_validate (msg) == 0) {
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
    json_t *json = NULL;
    json_error_t err;
    int saved_errno;

    msg_lasterr_reset (msg);

    if (msg_validate (msg) < 0)
        return -1;
    if (!fmt || *fmt == '\0')
        goto error_inval;

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

int flux_msg_get_payload (const flux_msg_t *msg, const void **buf, size_t *size)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!msg_has_payload (msg)) {
        errno = EPROTO;
        return -1;
    }
    if (buf)
        *buf = msg->payload;
    if (size)
        *size = msg->payload_size;
    return 0;
}

bool flux_msg_has_payload (const flux_msg_t *msg)
{
    if (msg_validate (msg) < 0)
        return false;
    return msg_has_payload (msg) ? true : false;
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
    const char *result;
    size_t size;

    if (msg_validate (msg) < 0)
        return -1;
    if (flux_msg_get_payload (msg, (const void **)&buf, &size) < 0) {
        errno = 0;
        result = NULL;
    } else {
        if (!buf || size == 0 || buf[size - 1] != '\0') {
            errno = EPROTO;
            return -1;
        }
        result = buf;
    }
    if (s)
        *s = result;
    return 0;
}

/* N.B. const attribute of msg argument is defeated internally to
 * allow msg to be "annotated" with parsed json object for convenience.
 * The message content is otherwise unchanged.
 */
int flux_msg_vunpack (const flux_msg_t *cmsg, const char *fmt, va_list ap)
{
    const char *json_str;
    json_error_t err;
    flux_msg_t *msg = (flux_msg_t *)cmsg;

    msg_lasterr_reset (msg);

    if (msg_validate (msg) < 0)
        return -1;
    if (!fmt || *fmt == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!msg->json) {
        if (flux_msg_get_string (msg, &json_str) < 0) {
            msg_lasterr_set (msg, "flux_msg_get_string: %s", strerror (errno));
            return -1;
        }
        if (!json_str) {
            msg_lasterr_set (msg, "message does not have a string payload");
            errno = EPROTO;
            return -1;
        }
        if (!(msg->json = json_loads (json_str, JSON_ALLOW_NUL, &err))) {
            msg_lasterr_set (msg, "%s", err.text);
            errno = EPROTO;
            return -1;
        }
        if (!json_is_object (msg->json)) {
            msg_lasterr_set (msg, "payload is not a JSON object");
            errno = EPROTO;
            return -1;
        }
    }
    if (json_vunpack_ex (msg->json, &err, 0, fmt, ap) < 0) {
        msg_lasterr_set (msg, "%s", err.text);
        errno = EPROTO;
        return -1;
    }
    return 0;
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
    if (msg->refcount <= 0)
        return "msg object is unreferenced";
    if (msg->lasterr == NULL)
        return "";
    return msg->lasterr;
}

int flux_msg_set_topic (flux_msg_t *msg, const char *topic)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (msg_is_control (msg)) {
        errno = EINVAL;
        return -1;
    }
    if (msg_has_topic (msg) && topic) {         /* case 1: replace topic */
        free (msg->topic);
        if (!(msg->topic = strdup (topic)))
            return -1;
    } else if (!msg_has_topic (msg) && topic) { /* case 2: add topic */
        if (!(msg->topic = strdup (topic)))
            return -1;
        msg_set_flag (msg, FLUX_MSGFLAG_TOPIC);
    } else if (msg_has_topic (msg) && !topic) { /* case 3: delete topic */
        free (msg->topic);
        msg->topic = NULL;
        msg_clear_flag (msg, FLUX_MSGFLAG_TOPIC);
    }
    return 0;
}

int flux_msg_get_topic (const flux_msg_t *msg, const char **topic)
{
    if (msg_validate (msg) < 0)
        return -1;
    if (!topic) {
        errno = EINVAL;
        return -1;
    }
    if (!msg_has_topic (msg)) {
        errno = EPROTO;
        return -1;
    }
    *topic = msg->topic;
    return 0;
}

flux_msg_t *flux_msg_copy (const flux_msg_t *msg, bool payload)
{
    flux_msg_t *cpy = NULL;

    if (msg_validate (msg) < 0)
        return NULL;

    if (!(cpy = msg_create ()))
        return NULL;

    cpy->proto = msg->proto;

    if (flux_msg_route_count (msg) > 0) {
        struct route_id *r = NULL;
        list_for_each_rev (&msg->routes, r, route_id_node) {
            if (flux_msg_route_push (cpy, r->id) < 0)
                goto error;
        }
    }
    if (msg->topic) {
        if (!(cpy->topic = strdup (msg->topic)))
            goto nomem;
    }
    if (msg->payload) {
        if (payload) {
            cpy->payload_size = msg->payload_size;
            if (!(cpy->payload = malloc (cpy->payload_size)))
                goto error;
            memcpy (cpy->payload, msg->payload, msg->payload_size);
        }
        else
            msg_clear_flag (cpy, FLUX_MSGFLAG_PAYLOAD);
    }
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
    { "control", "c", FLUX_MSGTYPE_CONTROL},
};

const char *flux_msg_typestr (int type)
{
    int i;

    for (i = 0; i < ARRAY_SIZE (typemap); i++)
        if ((type & typemap[i].type))
            return typemap[i].name;
    return "unknown";
}

static const char *type2prefix (int type)
{
    int i;

    for (i = 0; i < ARRAY_SIZE (typemap); i++)
        if ((type & typemap[i].type))
            return typemap[i].sname;
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

static void flags2str (int flags, char *buf, int buflen)
{
    int i, len = 0;
    buf[0] = '\0';
    for (i = 0; i < ARRAY_SIZE (flagmap); i++) {
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
    if (userid == FLUX_USERID_UNKNOWN)
        (void)snprintf (buf, buflen, "unknown");
    else
        (void)snprintf (buf, buflen, "%u", userid);
}

static int roletostr (uint32_t role, const char *sep, char *buf, int buflen)
{
    int n;
    if (role == FLUX_ROLE_OWNER)
        n = snprintf (buf, buflen, "%sowner", sep);
    else if (role == FLUX_ROLE_USER)
        n = snprintf (buf, buflen, "%suser", sep);
    else if (role == FLUX_ROLE_LOCAL)
        n = snprintf (buf, buflen, "%slocal", sep);
    else
        n = snprintf (buf, buflen, "%s0x%x", sep, role);
    if (n >= buflen)
        n = buflen;
    return n;
}

static void rolemask2str (uint32_t rolemask, char *buf, int buflen)
{
    if (rolemask == FLUX_ROLE_NONE)
        snprintf (buf, buflen, "none");
    else if (rolemask == FLUX_ROLE_ALL)
        snprintf (buf, buflen, "all");
    else {
        int offset = 0;
        for (int i = 0; i < sizeof (rolemask)*8; i++) {
            if (rolemask & 1<<i) {
                offset += roletostr (rolemask & 1<<i,
                                     offset > 0 ? "," : "",
                                     buf + offset,
                                     buflen - offset);
            }
        }
    }
}

static void nodeid2str (uint32_t nodeid, char *buf, int buflen)
{
    if (nodeid == FLUX_NODEID_ANY)
        (void)snprintf (buf, buflen, "any");
    else if (nodeid == FLUX_NODEID_UPSTREAM)
        (void)snprintf (buf, buflen, "upstream");
    else
        (void)snprintf (buf, buflen, "%u", nodeid);
}

void flux_msg_fprint_ts (FILE *f, const flux_msg_t *msg, double timestamp)
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
    if (msg->refcount <= 0) {
        fprintf (f, "unref");
        return;
    }
    prefix = type2prefix (msg_typeof (msg));
    /* Timestamp
     */
    if (timestamp >= 0.)
        fprintf (f, "%s %.5f\n", prefix, timestamp);
    /* Topic (control has none)
     */
    if (msg->topic)
        fprintf (f, "%s %s\n", prefix, msg->topic);
    /* Proto info
     */
    flags2str (msg->proto.flags, flagsstr, sizeof (flagsstr));
    userid2str (msg->proto.userid, useridstr, sizeof (useridstr));
    rolemask2str (msg->proto.rolemask, rolemaskstr, sizeof (rolemaskstr));
    fprintf (f, "%s flags=%s userid=%s rolemask=%s ",
             prefix, flagsstr, useridstr, rolemaskstr);
    switch (msg_typeof (msg)) {
        case FLUX_MSGTYPE_REQUEST:
            nodeid2str (msg->proto.nodeid, nodeidstr, sizeof (nodeidstr));
            fprintf (f, "nodeid=%s matchtag=%u\n",
                     nodeidstr,
                     msg->proto.matchtag);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            fprintf (f, "errnum=%u matchtag=%u\n",
                     msg->proto.errnum,
                     msg->proto.matchtag);
            break;
        case FLUX_MSGTYPE_EVENT:
            fprintf (f, "sequence=%u\n",
                     msg->proto.sequence);
            break;
        case FLUX_MSGTYPE_CONTROL:
            fprintf (f, "type=%u status=%u\n",
                     msg->proto.control_type,
                     msg->proto.control_status);
            break;
        default:
            fprintf (f, "aux1=0x%X aux2=0x%X\n",
                     msg->proto.aux1,
                     msg->proto.aux2);
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
        size_t size;
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

void flux_msg_fprint (FILE *f, const flux_msg_t *msg)
{
    flux_msg_fprint_ts (f, msg, -1);
}

int msg_frames (const flux_msg_t *msg)
{
    int n = 1; /* 1 for proto frame */
    if (msg_validate (msg) < 0)
        return -1;
    if (msg_has_payload (msg))
        n++;
    if (msg_has_topic (msg))
        n++;
    if (msg_has_route (msg)) {
        /* +1 for routes delimiter frame */
        n += msg->routes_len + 1;
    }
    return n;
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

int flux_match_asprintf (struct flux_match *m, const char *fmt, ...)
{
    va_list ap;
    char *topic = NULL;
    int res;

    va_start (ap, fmt);
    res = vasprintf (&topic, fmt, ap);
    va_end (ap);
    if (res < 0)
        return -1;

    m->topic_glob = topic;
    return 0;
}

bool flux_msg_route_match_first (const flux_msg_t *msg1,
                                 const flux_msg_t *msg2)
{
    const char *id1 = flux_msg_route_first (msg1);
    const char *id2 = flux_msg_route_first (msg2);

    if (!id1 && !id2)
        return true;
    if (id1 && id2 && streq (id1, id2))
        return true;
    return false;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
