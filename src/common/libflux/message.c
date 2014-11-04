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

#include "message.h"

#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"


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

/* Return a non-routing frame by number, zero origin
 * 0=tag/topic, 1=json
 */
static zframe_t *unwrap_zmsg (zmsg_t *zmsg, int frameno)
{
    zframe_t *zf = zmsg_first (zmsg);

    while (zf && zframe_size (zf) != 0)
        zf = zmsg_next (zmsg); /* skip non-empty routing envelope frames */
    if (zf)
        zf = zmsg_next (zmsg); /* skip empty routing envelope delimiter */
    if (!zf)
        zf = zmsg_first (zmsg); /* rewind - there was no routing envelope */
    while (zf && frameno-- > 0)
        zf = zmsg_next (zmsg);
    return zf;
}

/* Return routing frame by hop, not including delimiter.
 * -1=sender (frame closest to delim),
 *  0=next hop (frame furthest from delim)
 */
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

    if (!tag || strlen (tag) == 0) {
        errno = EINVAL;
        return NULL;
    }
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

bool flux_msg_match (zmsg_t *zmsg, const char *s)
{
    zframe_t *zf = unwrap_zmsg (zmsg, 0); /* tag/topic frame */
    return zf ? zframe_streq (zf, s) : false;
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

#ifdef TEST_MAIN
#include "src/common/libtap/tap.h"
#include "src/common/libutil/shortjson.h"
int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    //zframe_t *zf;
    json_object *o = NULL;
    char *s = NULL;
    int rc, i;

    plan (23);

    /* flux_msg_encode, flux_msg_decode, flux_msg_match
     *   on message with no JSON frame
     */
    zmsg = flux_msg_encode (NULL, NULL);
    ok (zmsg == NULL,
        "flux_msg_encode with NULL topic string fails with errno == EINVAL");
    zmsg_destroy (&zmsg); // ok with NULL zmsg

    zmsg = flux_msg_encode ("foo", NULL);
    ok (zmsg != NULL,
        "flux_msg_encode with NULL json works");

    ok ((!flux_msg_match (zmsg, "f")
            && flux_msg_match (zmsg, "foo")
            && !flux_msg_match (zmsg, "foobar")),
        "flux_msg_match works");

    o = (json_object *)&o; // make it non-NULL
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
    ok (!strcmp (s, "a.b.c.d"),
        "flux_msg_decode returned topic string");
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

    /* flux_msg_replace_json, flux_msg_replace_json_errnum
     *   on message with and without JSON frame
     */
    zmsg = flux_msg_encode ("baz", NULL);
    o = Jnew ();
    Jadd_int (o, "x", 2);
    rc = flux_msg_replace_json (zmsg, o);
    ok ((rc == -1 && errno == EPROTO),
        "flux_msg_replace_json fails with EPROTO on json-less message");
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
    rc = flux_msg_replace_json_errnum (zmsg, ESRCH);
    ok ((rc == 0),
        "flux_msg_replace_json_errnum worked");
    rc = flux_msg_decode (zmsg, NULL, &o);
    ok ((rc == 0 && Jget_int (o, "errnum", &i) && i == ESRCH),
        "flux_msg_decode returned errnum with correct value");
    Jput (o);
    zmsg_destroy (&zmsg);

    done_testing();
    return (0);
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

