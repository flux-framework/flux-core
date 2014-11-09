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

#define PROTO_MAGIC 0x8e
#define PROTO_SIZE 7

/* Begin manual codec
 */
static int proto_set_nodeid (uint8_t *data, int len, uint32_t nodeid);
static int proto_set_errnum (uint8_t *data, int len, uint32_t errnum);
static int proto_set_seq (uint8_t *data, int len, uint32_t seq);

static int proto_set_type (uint8_t *data, int len, int type)
{
    if (len < 2 || data[0] != PROTO_MAGIC)
        return -1;
    data[1] = type;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            proto_set_nodeid (data, len, FLUX_NODEID_ANY);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            proto_set_errnum (data, len, 0);
            break;
        case FLUX_MSGTYPE_EVENT:
            proto_set_seq (data, len, 0);
            break;
    }
    return 0;
}
static int proto_get_type (uint8_t *data, int len, int *type)
{
    if (len < 2 || data[0] != PROTO_MAGIC)
        return -1;
    *type = data[1];
    return 0;
}
static int proto_set_flags (uint8_t *data, int len, uint8_t flags)
{
    if (len < 2 || data[0] != PROTO_MAGIC)
        return -1;
    data[2] = flags;
    return 0;
}
static int proto_get_flags (uint8_t *data, int len, uint8_t *val)
{
    if (len < 2 || data[0] != PROTO_MAGIC)
        return -1;
    *val = data[2];
    return 0;
}
static int proto_set_nodeid (uint8_t *data, int len, uint32_t nodeid)
{
    uint32_t x = htonl (nodeid);

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_REQUEST)
        return -1;
    memcpy (&data[3], &x, sizeof (x));
    return 0;
}
static int proto_get_nodeid (uint8_t *data, int len, uint32_t *nodeid)
{
    uint32_t x;

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_REQUEST)
        return -1;
    memcpy (&x, &data[3], sizeof (x));
    *nodeid = ntohl (x);
    return 0;
}
static int proto_set_errnum (uint8_t *data, int len, uint32_t errnum)
{
    uint32_t x = htonl (errnum);

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_RESPONSE)
        return -1;
    memcpy (&data[3], &x, sizeof (x));
    return 0;
}
static int proto_get_errnum (uint8_t *data, int len, uint32_t *errnum)
{
    uint32_t x;

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_RESPONSE)
        return -1;
    memcpy (&x, &data[3], sizeof (x));
    *errnum = ntohl (x);
    return 0;
}
static int proto_set_seq (uint8_t *data, int len, uint32_t seq)
{
    uint32_t x = htonl (seq);

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_EVENT)
        return -1;
    memcpy (&data[3], &x, sizeof (x));
    return 0;
}
static int proto_get_seq (uint8_t *data, int len, uint32_t *seq)
{
    uint32_t x;

    if (len < 6 || data[0] != PROTO_MAGIC || data[1] != FLUX_MSGTYPE_EVENT)
        return -1;
    memcpy (&x, &data[3], sizeof (x));
    *seq = ntohl (x);
    return 0;
}
static void proto_init (uint8_t *data, int len, uint8_t flags)
{
    assert (len >= 3);
    memset (data, 0, len);
    data[0] = PROTO_MAGIC;
    data[2] = flags;
}

/* End manual codec
 */

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

static zframe_t *flux_msg_skip_routing (zmsg_t *zmsg)
{
    zframe_t *zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty routing envelope frames */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty routing envelope delimiter */
    else
        zf = zmsg_first (zmsg); /* rewind - there was no routing envelope */
    return zf;
}

static zframe_t *flux_msg_get_topic (zmsg_t *zmsg)
{
    uint8_t fl;
    if (flux_msg_get_flags (zmsg, &fl) < 0 || !(fl & FLUX_MSGFLAG_TOPIC))
        return NULL;
    return flux_msg_skip_routing (zmsg);
}

static zframe_t *flux_msg_get_payload (zmsg_t *zmsg)
{
    zframe_t *zf;
    uint8_t fl;

    if (flux_msg_get_flags (zmsg, &fl) < 0 || !(fl & FLUX_MSGFLAG_PAYLOAD))
        return NULL;
    if (!(zf = flux_msg_skip_routing (zmsg)))
        return NULL;
    if ((fl & FLUX_MSGFLAG_TOPIC))
        zf = zmsg_next (zmsg);
    return zf;
}

static zframe_t *flux_msg_get_json (zmsg_t *zmsg)
{
    uint8_t fl;
    if (flux_msg_get_flags (zmsg, &fl) < 0 || !(fl & FLUX_MSGFLAG_JSON))
        return NULL;
    return flux_msg_get_payload (zmsg);
}

int flux_msg_decode (zmsg_t *zmsg, char **topic, JSON *o)
{
    zframe_t *zf;
    int rc = -1;
    if (topic) {
        if ((zf = flux_msg_get_topic (zmsg))) {
            if (!(*topic = zframe_strdup (zf)))
                oom ();
        } else
            *topic = NULL;
    }
    if (o) {
        if ((zf = flux_msg_get_json (zmsg)))
            util_json_decode (o, (char *)zframe_data (zf), zframe_size (zf));
        else
            *o = NULL;
    }
    rc = 0;
    return rc;
}

zmsg_t *flux_msg_create (int type, const char *topic, void *buf, int len)
{
    uint8_t proto[PROTO_SIZE];
    uint8_t fl = 0;
    zmsg_t *zmsg;
    int rc;

    if (!(zmsg = zmsg_new ()))
        oom ();
    if (topic) {
        if (zmsg_addmem (zmsg, topic, strlen (topic)) < 0)
            oom ();
        fl |= FLUX_MSGFLAG_TOPIC;
    }
    if (buf && len > 0) {
        if (zmsg_addmem (zmsg, buf, len) < 0)
            oom ();
        fl |= FLUX_MSGFLAG_PAYLOAD;
    }
    proto_init (proto, PROTO_SIZE, fl);
    rc = proto_set_type (proto, PROTO_SIZE, type);
    assert (rc == 0);
    if (zmsg_addmem (zmsg, proto, PROTO_SIZE) < 0)
        oom ();
    return zmsg;
}

zmsg_t *flux_msg_encode (char *topic, JSON o)
{
    int rc;
    zmsg_t *zmsg;
    unsigned int zlen = 0;
    char *zbuf = NULL;
    uint8_t fl;

    if (o) {
        util_json_encode (o, &zbuf, &zlen);
        if ((zmsg = flux_msg_create (0, topic, zbuf, zlen))) {
            rc = flux_msg_get_flags (zmsg, &fl);
            assert (rc == 0);
            rc = flux_msg_set_flags (zmsg, fl | FLUX_MSGFLAG_JSON);
            assert (rc == 0);
        }
    } else
        zmsg = flux_msg_create (0, topic, NULL, 0);
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

int flux_msg_set_flags (zmsg_t *zmsg, uint8_t fl)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_set_flags (zframe_data (zf), zframe_size (zf), fl) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_flags (zmsg_t *zmsg, uint8_t *fl)
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
    if (!zf || proto_set_nodeid (zframe_data (zf), zframe_size (zf), nid) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nid)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_get_nodeid (zframe_data (zf), zframe_size (zf), nid) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

int flux_msg_set_errnum (zmsg_t *zmsg, int e)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_set_errnum (zframe_data (zf), zframe_size (zf), e) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_errnum (zmsg_t *zmsg, int *e)
{
    zframe_t *zf = zmsg_last (zmsg);
    uint32_t xe;

    if (!zf || proto_get_errnum (zframe_data (zf), zframe_size (zf), &xe) < 0) {
        errno = EPROTO;
        return -1;
    }
    *e = xe;
    return 0;
}

int flux_msg_set_seq (zmsg_t *zmsg, uint32_t seq)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_set_seq (zframe_data (zf), zframe_size (zf), seq) < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_msg_get_seq (zmsg_t *zmsg, uint32_t *seq)
{
    zframe_t *zf = zmsg_last (zmsg);
    if (!zf || proto_get_seq (zframe_data (zf), zframe_size (zf), seq) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

bool flux_msg_match (zmsg_t *zmsg, const char *topic)
{
    zframe_t *zf = flux_msg_get_topic (zmsg);
    return zf ? zframe_streq (zf, topic) : false;
}

/* Return routing frame closest to the delimiter, the request sender.
 * Caller must free result.  On error return NULL with errno set.
 */
char *flux_msg_sender (zmsg_t *zmsg)
{
    int hops = flux_msg_hopcount (zmsg);
    zframe_t *zf;
    char *uuid = NULL;

    if (hops == 0) {
        errno = EPROTO;
        goto done;
    }
    zf = zmsg_first (zmsg);
    while (--hops > 0)
        zf = zmsg_next (zmsg);
    if (!(uuid = zframe_strdup (zf))) {
        errno = ENOMEM;
        goto done;
    }
done:
    return uuid;
}

/* Return routing frame at the top of the stack, the next hop in a response.
 * Caller must free result. On error, return NULL with errno set.
 */
char *flux_msg_nexthop (zmsg_t *zmsg)
{
    int hops = flux_msg_hopcount (zmsg);
    zframe_t *zf;
    char *uuid = NULL;

    if (hops == 0) {
        errno = EPROTO;
        goto done;
    }
    zf = zmsg_first (zmsg);
    if (!(uuid = zframe_strdup (zf))) {
        errno = ENOMEM;
        goto done;
    }
done:
    return uuid;
}

char *flux_msg_tag (zmsg_t *zmsg)
{
    zframe_t *zf = flux_msg_get_topic (zmsg);
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

int flux_msg_replace_json (zmsg_t *zmsg, JSON o)
{
    zframe_t *zf;
    char *zbuf;
    unsigned int zlen;
    int rc = -1;
    uint8_t fl;

    if (flux_msg_get_flags (zmsg, &fl) < 0) {
        errno = EPROTO;
        goto done;
    }
    if ((zf = flux_msg_get_json (zmsg)) && o != NULL) {
        util_json_encode (o, &zbuf, &zlen);
        zframe_reset (zf, zbuf, zlen);
        free (zbuf);
    } else if (zf && o == NULL) {
        zmsg_remove (zmsg, zf);
        zframe_destroy (&zf);
        fl &= ~(uint8_t)(FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_JSON);
    } else if (!zf && o != NULL) {
        if (!(zf = zmsg_last (zmsg))) {
            errno = EPROTO;
            goto done;
        }
        zmsg_remove (zmsg, zf);
        util_json_encode (o, &zbuf, &zlen);
        if (zmsg_addmem (zmsg, zbuf, zlen) < 0 || zmsg_append (zmsg, &zf) < 0)
            oom ();
        free (zbuf);
        fl |= (FLUX_MSGFLAG_PAYLOAD | FLUX_MSGFLAG_JSON);
    }
    if (flux_msg_set_flags (zmsg, fl) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
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

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"
#include "src/common/libutil/shortjson.h"
int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    //zframe_t *zf;
    JSON o = NULL;
    char *s = NULL;
    int rc, i;
    int type, errnum;
    uint32_t nodeid;

    plan (32);

    /* flux_msg_encode, flux_msg_decode, flux_msg_match
     *   on message with no JSON frame
     */
    zmsg = flux_msg_encode (NULL, NULL);
    ok ((zmsg != NULL),
        "flux_msg_encode with NULL topic and json works");
    zmsg_destroy (&zmsg);

    zmsg = flux_msg_encode ("foo", NULL);
    ok (zmsg != NULL,
        "flux_msg_encode with NULL json works");

    ok ((!flux_msg_match (zmsg, "f")
            && flux_msg_match (zmsg, "foo")
            && !flux_msg_match (zmsg, "foobar")),
        "flux_msg_match works");

    o = (JSON)&o; // make it non-NULL
    rc = flux_msg_decode (zmsg, &s, &o);

    ok ((rc == 0 && s && !strcmp (s, "foo") && o == NULL),
        "flux_msg_decode returned what we encoded");
    free (s);
    zmsg_destroy (&zmsg);

    /* flux_msg_encode, flux_msg_decode, flux_msg_tag, flux_msg_tag_short
     *   on message with JSON
     */
    o = Jnew ();
    Jadd_int (o, "x", 42);
    zmsg = flux_msg_encode ("a.b.c.d", o);
    Jput (o);
    ok (zmsg != NULL,
        "flux_msg_encode with json works");
    rc = flux_msg_decode (zmsg, &s, &o);
    ok ((rc == 0 && Jget_int (o, "x",&i) && i == 42),
        "flux_msg_decode returned JSON that we encoded");
    Jput (o);
    ok (s != NULL && !strcmp (s, "a.b.c.d"),
        "flux_msg_decode returned topic string");
    if (s)
        free (s);
    s = flux_msg_tag (zmsg);
    ok (!strcmp (s, "a.b.c.d"),
        "flux_msg_tag returned topic string");
    free (s);
    s = flux_msg_tag_short (zmsg);
    ok (!strcmp (s, "a"),
        "flux_msg_tag_short returned first word of topic string");
    free (s);
    zmsg_destroy (&zmsg);

    /* flux_msg_sender, flux_msg_hopcount, flux_msg_nexthop
     *   on message with variable number of routing frames
     */
    zmsg = flux_msg_encode ("foo", NULL);
    ok ((zmsg && flux_msg_hopcount (zmsg) == 0),
        "flux_msg_hopcount on message with no delim is 0");
    ok ((!flux_msg_sender (zmsg) && !flux_msg_nexthop (zmsg)),
        "flux_msg_sender, flux_msg_nexthop return NULL on msg w/no delim");
    if (zmsg)
        zmsg_pushmem (zmsg, NULL, 0); /* push nil delimiter frame */
    ok ((zmsg && flux_msg_hopcount (zmsg) == 0),
        "flux_msg_hopcount on message with delim is 0");
    ok ((!flux_msg_sender (zmsg) && !flux_msg_nexthop (zmsg)),
        "flux_msg_sender, flux_msg_nexthop return NULL on msg w/delim, no id");
    zmsg_pushstrf (zmsg, "%s", "sender");
    ok ((zmsg && flux_msg_hopcount (zmsg) == 1),
        "flux_msg_hopcount on message with delim+id is 1");
    s = flux_msg_sender (zmsg);
    ok ((s && !strcmp (s, "sender")),
        "flux_msg_sender returns id on message with delim+id");
    free (s);
    s = flux_msg_nexthop (zmsg);
    ok ((s && !strcmp (s, "sender")),
        "flux_msg_nexthop returns id on message with delim+id");
    free (s);
    zmsg_pushstrf (zmsg, "%s", "router");
    s = flux_msg_sender (zmsg);
    ok ((s && !strcmp (s, "sender")),
        "flux_msg_sender returns id on message with delim+id1+id2");
    free (s);
    s = flux_msg_nexthop (zmsg);
    ok ((s && !strcmp (s, "router")),
        "flux_msg_nexthop returns id2 on message with delim+id1+id2");
    free (s);
    zmsg_destroy (&zmsg);

    /* flux_msg_replace_json
     *   on message with and without JSON frame
     */
    zmsg = flux_msg_encode ("baz", NULL);
    o = Jnew ();
    Jadd_int (o, "x", 2);
    rc = flux_msg_replace_json (zmsg, o);
    ok ((rc == 0),
        "flux_msg_replace_json works on json-less message");
    zmsg_destroy (&zmsg);
    zmsg = flux_msg_encode ("baz", o);
    Jput (o);
    o = Jnew ();
    Jadd_int (o, "y", 3);
    rc = flux_msg_replace_json (zmsg, o);
    Jput (o);
    ok ((rc == 0),
        "flux_msg_replace_json works json-ful message");
    rc = flux_msg_decode (zmsg, NULL, &o);
    ok ((rc == 0 && Jget_int (o, "y", &i) && i == 3),
        "flux_msg_decode returned replaced json");
    Jput (o);
    zmsg_destroy (&zmsg);

    /* flux_msg_set_type, flux_msg_get_type
     * flux_msg_set_nodeid, flux_msg_get_nodeid
     * flux_msg_set_errnum, flux_msg_get_errnum
     */
    zmsg = flux_msg_encode ("rat", NULL);
    rc = flux_msg_set_type (zmsg, FLUX_MSGTYPE_REQUEST);
    ok ((rc == 0),
        "flux_msg_set_type works");
    rc = flux_msg_get_type (zmsg, &type);
    ok ((rc == 0 && type == FLUX_MSGTYPE_REQUEST),
        "flux_msg_get_type works and returns what we set");
    rc = flux_msg_get_nodeid (zmsg, &nodeid);
    ok ((rc == 0 && nodeid == FLUX_NODEID_ANY),
        "flux_msg_get_nodeid works and default is sane");
    nodeid = 42;
    rc = flux_msg_set_nodeid (zmsg, nodeid);
    ok ((rc == 0),
        "flux_msg_set_nodeid works");
    rc = flux_msg_get_nodeid (zmsg, &nodeid);
    ok ((rc == 0 && nodeid == 42),
        "flux_msg_get_nodeid works and returns what we set");
    rc = flux_msg_set_errnum (zmsg, 42);
    ok ((rc < 0 && errno == EINVAL),
        "flux_msg_set_errnum on non-response fails with errno == EINVAL");
    rc = flux_msg_set_type (zmsg, FLUX_MSGTYPE_RESPONSE);
    ok ((rc == 0),
        "flux_msg_set_type works");
    rc = flux_msg_get_type (zmsg, &type);
    ok ((rc == 0 && type == FLUX_MSGTYPE_RESPONSE),
        "flux_msg_get_type works and returns what we set");
    rc = flux_msg_set_nodeid (zmsg, 0);
    ok ((rc < 0 && errno == EINVAL),
        "flux_msg_set_nodeid on non-request fails with errno == EINVAL");
    rc = flux_msg_set_errnum (zmsg, 43);
    ok ((rc == 0),
        "flux_msg_set_errnum works");
    rc = flux_msg_get_errnum (zmsg, &errnum);
    ok ((rc == 0 && errnum == 43),
        "flux_msg_get_errnum works and returns what we set");
    zmsg_destroy (&zmsg);

    done_testing();
    return (0);
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

