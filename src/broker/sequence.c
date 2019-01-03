/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*
 *  Simple class for named sequences service
 */
#ifndef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdbool.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "sequence.h"

typedef struct {
    zhash_t *vhash;
    flux_msg_handler_t **handlers;
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
    rc = zhash_insert (s->vhash, name, v);
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

static int handle_seq_destroy (flux_t *h, seqhash_t *s, const flux_msg_t *msg)
{
    const char *name;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "name", &name) < 0)
        return (-1);
    if (seq_destroy (s, name) < 0)
        return (-1);
    return flux_respond_pack (h, msg, "{ s:s s:b }",
                              "name", name,
                              "destroyed", true);
}

static int handle_seq_set (flux_t *h, seqhash_t *s, const flux_msg_t *msg)
{
    const char *name;
    int64_t old, v;

    if (flux_request_unpack (msg, NULL, "{ s:s s:I }",
                             "name", &name,
                             "value", &v) < 0)
        return (-1);

    if (!flux_request_unpack (msg, NULL, "{ s:I }", "oldvalue", &old)
        && seq_cmp_and_set (s, name, old, v) < 0)
        return (-1);
    else if (seq_set (s, name, v) < 0)
        return (-1);

    if (flux_respond_pack (h, msg, "{ s:s s:b s:I }",
                           "name", name,
                           "set", true,
                           "value", v) < 0)
        return (-1);

    return (0);
}

static int handle_seq_fetch (flux_t *h, seqhash_t *s, const flux_msg_t *msg)
{
    const char *name;
    int create = false;
    bool created = false;
    int64_t v, pre, post, *valp;

    if (flux_request_unpack (msg, NULL, "{ s:s s:b s:I s:I }",
                             "name", &name,
                             "create", &create,
                             "preincrement", &pre,
                             "postincrement", &post) < 0)
        return (-1);

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

    if (create && created)
        return flux_respond_pack (h, msg, "{ s:s s:I s:b }",
                                  "name", name,
                                  "value", v,
                                  "created", true);

    return flux_respond_pack (h, msg, "{ s:s s:I }",
                              "name", name,
                              "value", v);
}

static void sequence_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                 const flux_msg_t *msg, void *arg)
{
    seqhash_t *seq = arg;
    const char *topic;
    int rc = -1;

    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;

    if (strcmp (topic, "seq.fetch") == 0)
        rc = handle_seq_fetch (h, seq, msg);
    else if (strcmp (topic, "seq.set") == 0)
        rc = handle_seq_set (h, seq, msg);
    else if (strcmp (topic, "seq.destroy") == 0)
        rc = handle_seq_destroy (h, seq, msg);
    else
        errno = ENOSYS;
done:
    if (rc < 0) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", topic);
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "seq.*",     sequence_request_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void sequence_hash_finalize (void *arg)
{
    seqhash_t *seq = arg;
    flux_msg_handler_delvec (seq->handlers);
    sequence_hash_destroy (seq);
}

int sequence_hash_initialize (flux_t *h)
{
    seqhash_t *seq = sequence_hash_create ();
    if (!seq)
        return -1;
    if (flux_msg_handler_addvec (h, htab, seq, &seq->handlers) < 0) {
        sequence_hash_destroy (seq);
        return -1;
    }
    flux_aux_set (h, "flux::sequence_hash", seq, sequence_hash_finalize);
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
