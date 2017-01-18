/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
/*
 *  Simple class for named sequences service
 */
#ifndef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdbool.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"
#include "sequence.h"

typedef struct {
    zhash_t *vhash;
} seqhash_t;

static seqhash_t * sequence_hash_create (void)
{
    seqhash_t *s;
    zhash_t *zh;
    if ((zh = zhash_new ()) == NULL)
        return (NULL);
    s = xzmalloc (sizeof (*s));
    s->vhash = zh;
    return (s);
}

static void sequence_hash_destroy (seqhash_t *s)
{
    zhash_destroy (&s->vhash);
    free (s);
}

static int64_t * seq_new (void)
{
    int64_t *v = xzmalloc (sizeof (*v));
    *v = 0;
    return (v);
}

static int64_t * seq_create (seqhash_t *s, const char *name)
{
    int rc;
    int64_t *v;

    if (zhash_lookup (s->vhash, name)) {
        errno = EEXIST;
        return (NULL);
    }
    v = seq_new ();
    rc = zhash_insert (s->vhash, xstrdup (name), v);
    assert (rc >= 0);
    zhash_freefn (s->vhash, name, free);
    return (v);
}

static int seq_destroy (seqhash_t *s, const char *name)
{
    if (!zhash_lookup (s->vhash, name)) {
        errno = ENOENT;
        return (-1);
    }
    zhash_delete (s->vhash, name);
    return (0);
}

static int seq_fetch_and_add (seqhash_t *s, const char *name,
                              int64_t preinc, int64_t postinc,
                              int64_t *valp)
{
    int64_t *v = zhash_lookup (s->vhash, name);
    if (v == NULL) {
        errno = ENOENT;
        return (-1);
    }
    *v += preinc;
    *valp = *v;
    *v += postinc;
    return (0);
}


static int seq_set (seqhash_t *s, const char *name, int64_t val)
{
    int64_t *v = zhash_lookup (s->vhash, name);
    if (v == NULL) {
        errno = ENOENT;
        return (-1);
    }
    *v = val;
    return (0);
}

static int seq_cmp_and_set (seqhash_t *s, const char *name,
                            int64_t oldval, int64_t newval)
{
    int64_t *v = zhash_lookup (s->vhash, name);
    if (v == NULL) {
        errno = ENOENT;
        return (-1);
    }
    if (*v != oldval) {
        errno = EAGAIN;
        return (-1);
    }
    *v = newval;
    return (0);
}

static int handle_seq_destroy (seqhash_t *s, json_object *in,
			       json_object **outp)
{
    const char *name;
    if (!Jget_str (in, "name", &name)) {
        errno = EPROTO;
        return (-1);
    }
    if (seq_destroy (s, name) < 0)
        return (-1);
    *outp = Jnew ();
    Jadd_str (*outp, "name", name);
    Jadd_bool (*outp, "destroyed", true);
    return (0);
}

static int handle_seq_set (seqhash_t *s, json_object *in, json_object **outp)
{
    const char *name;
    int64_t old, v;
    if (!Jget_str (in, "name", &name)
        || !Jget_int64 (in, "value", &v)) {
        errno = EPROTO;
        return (-1);
    }
    if ((Jget_int64 (in, "oldvalue", &old)
        && (seq_cmp_and_set (s, name, old, v) < 0))
        || (seq_set (s, name, v) < 0))
            return (-1);

    *outp = Jnew ();
    Jadd_str (*outp, "name", name);
    Jadd_bool (*outp, "set", true);
    Jadd_int64 (*outp, "value", v);
    return (0);
}

static int handle_seq_fetch (seqhash_t *s, json_object *in, json_object **outp)
{
    const char *name;
    bool create = false;
    bool created = false;
    int64_t v, pre, post, *valp;

    if (!Jget_str (in, "name", &name)
        || !Jget_bool (in, "create", &create)
        || !Jget_int64 (in, "preincrement", &pre)
        || !Jget_int64 (in, "postincrement", &post)) {
        errno = EPROTO;
        return (-1);
    }
    if (seq_fetch_and_add (s, name, pre, post, &v) < 0) {
        if (!create || (errno != ENOENT))
            return (-1);
        /*  Create and initialize
         */
        valp = seq_create (s, name);
        *valp += pre;
        v = *valp;
        *valp += post;
        created = true;
    }

    *outp = Jnew ();
    Jadd_str (*outp, "name", name);
    Jadd_int64 (*outp, "value", v);
    if (create && created)
        Jadd_bool (*outp, "created", true);
    return (0);
}

static int sequence_request_handler (seqhash_t *s, const flux_msg_t *msg,
			                         json_object **outp)
{
    const char *json_str, *topic;
    json_object *in = NULL;
    int rc = -1;

    *outp = NULL;
    if (flux_request_decode (msg, &topic, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }

    if (strcmp (topic, "seq.fetch") == 0)
        rc = handle_seq_fetch (s, in, outp);
    else if (strcmp (topic, "seq.set") == 0)
        rc = handle_seq_set (s, in, outp);
    else if (strcmp (topic, "seq.destroy") == 0)
        rc = handle_seq_destroy (s, in, outp);
    else
        errno = ENOSYS;
done:
    Jput (in);
    return (rc);
}

static void sequence_request_cb (flux_t *h, flux_msg_handler_t *w,
                                 const flux_msg_t *msg, void *arg)
{
    seqhash_t *seq = arg;
    json_object *out = NULL;
    int rc = sequence_request_handler (seq, msg, &out);
    if (flux_respond (h, msg, rc < 0 ? errno : 0, Jtostr (out)) < 0)
        flux_log_error (h, "seq: flux_respond");
    if (out)
        Jput (out);
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "seq.*",     sequence_request_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

static void sequence_hash_finalize (void *arg)
{
    seqhash_t *seq = arg;
    sequence_hash_destroy (seq);
    flux_msg_handler_delvec (handlers);
}

int sequence_hash_initialize (flux_t *h)
{
    seqhash_t *seq = sequence_hash_create ();
    if (!seq)
        return -1;
    if (flux_msg_handler_addvec (h, handlers, seq) < 0) {
        sequence_hash_destroy (seq);
        return -1;
    }
    flux_aux_set (h, "flux::sequence_hash", seq, sequence_hash_finalize);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
