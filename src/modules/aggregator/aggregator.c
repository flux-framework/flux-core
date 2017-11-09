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

/* aggregator.c - reduction based numerical aggreagator */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"

struct aggregator {
    flux_t *h;
    uint32_t rank;
    double default_timeout;
    double timer_scale;
    zhash_t *aggregates;
};

/*
 *  Single entry in an aggregate: a list of ids with a common value.
 */
struct aggregate_entry {
    nodeset_t *ids;
    int64_t value;
};


/*
 *  Representation of an aggregate. A unique kvs key, along with a
 *   list of aggregate entries as above. Each aggregate tracks its
 *   minimum, maximum, current count and expected total of entries.
 */
struct aggregate {
    struct aggregator *ctx;  /* Pointer back to containing aggregator        */
    flux_watcher_t *tw;      /* timeout watcher                              */
    double timeout;          /* timeout                                      */
    uint32_t fwd_count;      /* forward at this many                         */
    char *key;               /* KVS key into which to sink the aggregate     */
    int64_t max;             /* current max value                            */
    int64_t min;             /* current minimum value                        */
    uint32_t count;          /* count of current total entries               */
    uint32_t total;          /* expected total entries (used for sink)       */
    zlist_t *entries;        /* list of individual entries                   */
};

static void aggregate_entry_destroy (struct aggregate_entry *ae)
{
    if (ae) {
        nodeset_destroy (ae->ids);
        free (ae);
    }
}

static struct aggregate_entry * aggregate_entry_create (void)
{
    struct aggregate_entry *ae = calloc (1, sizeof (*ae));
    if (ae != NULL)
        ae->ids = nodeset_create ();
    return (ae);
}


/*  Search this aggregates entries for a value. Return entry if found
 */
static struct aggregate_entry *
    aggregate_entry_find (struct aggregate *ag, int64_t value)
{
    struct aggregate_entry *ae = zlist_first (ag->entries);
    while (ae) {
        if (ae->value == value)
            return (ae);
        ae = zlist_next (ag->entries);
    }
    return (NULL);
}


/*  Add a new entry to this aggregate. Update minimum, maximum.
 */
static struct aggregate_entry *
    aggregate_entry_add (struct aggregate *ag, int64_t value)
{
    struct aggregate_entry *ae = aggregate_entry_create ();
    if (ae) {
        ae->value = value;
        if (ag->max < value)
            ag->max = value;
        if (ag->min > value)
            ag->min = value;
        zlist_push (ag->entries, ae);
    }
    return (ae);
}

/*  Push a new (ids, value) pair onto aggregate `ag`.
 *   If an existing matching entry is found, add ids to its nodeset.
 *   o/w, add a new entry. In either case update current count with
 *   the number of `ids` added.
 */
static int aggregate_push (struct aggregate *ag, int64_t value, const char *ids)
{
    int count;
    struct aggregate_entry *ae = aggregate_entry_find (ag, value);
    if ((ae == NULL) && !(ae = aggregate_entry_add (ag, value)))
        return (-1);

    count = nodeset_count (ae->ids);
    if (!nodeset_add_string (ae->ids, ids))
        return (-1);

    /* Update count */
    ag->count += (nodeset_count (ae->ids) - count);

    return (0);
}

/*  Push JSON represenation of an aggregate onto existing aggregate `ag`
 */
static int aggregate_push_json (struct aggregate *ag, json_object *o)
{
    json_object_iter i;
    int64_t n64;
    json_object *entries = NULL;

    if (ag->total == 0 && (Jget_int64 (o, "total", &n64)))
        ag->total = n64;

    if (!Jget_obj (o, "entries", &entries)) {
        flux_log_error (ag->ctx->h, "No object 'entries'");
        return (-1);
    }

    json_object_object_foreachC (entries, i) {
        int64_t val = json_object_get_int64 (i.val);
        if (aggregate_push (ag, val, i.key) < 0) {
            flux_log_error (ag->ctx->h, "aggregate_push failed");
            return (-1);
        }
    }

    return (0);
}

static json_object *aggregate_tojson (struct aggregate *ag)
{
    struct aggregate_entry *ae;
    json_object *entries;
    json_object *o = Jnew ();
    Jadd_str (o, "key", ag->key);
    Jadd_int (o, "count", ag->count);
    Jadd_int (o, "total", ag->total);
    Jadd_double (o, "timeout", ag->timeout);
    Jadd_int64 (o, "min", ag->min);
    Jadd_int64 (o, "max", ag->max);

    entries = Jnew ();
    ae = zlist_first (ag->entries);
    while (ae) {
        Jadd_int64 (entries, nodeset_string (ae->ids), ae->value);
        ae = zlist_next (ag->entries);
    }
    json_object_object_add (o, "entries", entries);

    return (o);
}

/*
 *  Forward aggregate `ag` upstream
 */
static int aggregate_forward (flux_t *h, struct aggregate *ag)
{
    int rc = 0;
    flux_future_t *f;
    json_object *o = aggregate_tojson (ag);
    flux_log (h, LOG_INFO, "forward: %s: count=%d total=%d\n",
                 ag->key, ag->count, ag->total);
    if (!(f = flux_rpc (h, "aggregator.push", Jtostr (o),
                             FLUX_NODEID_UPSTREAM, 0)) ||
        (flux_future_get (f, NULL) < 0)) {
        flux_log_error (h, "flux_rpc: aggregator.push");
        rc = -1;
    }
    Jput (o);
    flux_future_destroy (f);
    return (rc);
}

static void aggregate_sink_abort (flux_t *h, struct aggregate *ag)
{
    flux_msg_t *msg = NULL;
    char *topic = NULL;

    flux_log (h, LOG_ERR, "sink: aborting aggregate %s\n", ag->key);

    if ((asprintf (&topic, "aggregator.abort.%s", ag->key)) < 0) {
        flux_log_error (h, "sink_abort: asprintf");
        goto out;
    }
    if ((msg = flux_event_encode (topic, "{ }")) == NULL) {
        flux_log_error (h, "flux_event_encode");
        goto out;
    }
    if (flux_send (h, msg, 0) < 0)
        flux_log_error (h, "flux_event_encode");
out:
    free (topic);
    flux_msg_destroy (msg);
}

static int aggregate_sink (flux_t *h, struct aggregate *ag)
{
    int rc = -1;
    json_object *o = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    flux_log (h, LOG_INFO, "sink: %s: count=%d total=%d",
                ag->key, ag->count, ag->total);

    /* Fail on key == "." */
    if (strcmp (ag->key, ".") == 0) {
        flux_log (h, LOG_ERR, "sink: refusing to sink to rootdir");
        goto out;
    }
    if (!(o = aggregate_tojson (ag))) {
        flux_log (h, LOG_ERR, "sink: aggregate_tojson failed");
        goto out;
    }
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "sink: flux_kvs_txn_create");
        goto out;
    }
    if (flux_kvs_txn_put (txn, 0, ag->key, Jtostr (o)) < 0) {
        flux_log_error (h, "sink: flux_kvs_txn_put");
        goto out;
    }
    if (!(f = flux_kvs_commit (h, 0, txn)) || flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "sink: flux_kvs_commit");
        goto out;
    }
    rc = 0;
out:
    flux_kvs_txn_destroy (txn);
    flux_future_destroy (f);
    Jput (o);
    return (rc);
}

static void aggregate_sink_again (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg)
{
    struct aggregate *ag = arg;
    flux_t *h = ag->ctx->h;
    if (aggregate_sink (h, ag) < 0)
        aggregate_sink_abort (h, ag);
    flux_watcher_destroy (w);
    zhash_delete (ag->ctx->aggregates, ag->key);
}


/*
 *   Push aggregate to kvs.
 */
static void aggregate_try_sink (flux_t *h, struct aggregate *ag)
{
    if (aggregate_sink (h, ag) < 0) {
        flux_watcher_t *w;
        double t = ag->timeout;
        if (t <= 1e-3)
            t = .250;
        flux_log (h, LOG_INFO, "sink: %s: retry  in %.3fs", ag->key, t);
        /* On failure, retry just once, then abort */
        w = flux_timer_watcher_create (flux_get_reactor (h),
                                       t, 0.,
                                       aggregate_sink_again,
                                       (void *) ag);
        if (w == NULL) {
            flux_log_error (h, "flux_timer_watcher_create");
            /* Force abort now */
            aggregate_sink_abort (h, ag);
            return;
        }
        flux_watcher_start (w);
        return;
    }
    zhash_delete (ag->ctx->aggregates, ag->key);
    return;
}

/*
 *  Flush aggregate `ag` -- forward entry upstream and destroy it locally.
 */
static int aggregate_flush (struct aggregate *ag)
{
    flux_t *h = ag->ctx->h;
    int rc;
    assert (ag->ctx->rank != 0);
    rc = aggregate_forward (h, ag);
    zhash_delete (ag->ctx->aggregates, ag->key);
    return (rc);
}

static void aggregate_destroy (struct aggregate *ag)
{
    struct aggregate_entry *ae = zlist_first (ag->entries);
    while (ae) {
        aggregate_entry_destroy (ae);
        ae = zlist_next (ag->entries);
    }
    zlist_destroy (&ag->entries);
    flux_watcher_destroy (ag->tw);
    free (ag->key);
    free (ag);
}

static void timer_cb (flux_reactor_t *r, flux_watcher_t *tw,
                      int revents, void *arg)
{
    if (aggregate_flush (arg) < 0) {
        flux_t *h = ((struct aggregate *) arg)->ctx->h;
        flux_log_error (h, "aggregate_flush");
    }
}

static void aggregate_timer_start (struct aggregator *ctx,
                                   struct aggregate *ag,
                                   double timeout)
{
    if (ctx->rank != 0) {
        flux_t *h = ctx->h;
        flux_reactor_t *r = flux_get_reactor (h);
        ag->tw = flux_timer_watcher_create (r, timeout, 0.,
                                            timer_cb, (void *) ag);
        if (ag->tw == NULL)
            flux_log_error (h, "flux_timer_watcher_create");
        flux_watcher_start (ag->tw);
    }
}

static struct aggregate *
    aggregate_create (struct aggregator *ctx, const char *key)
{
    flux_t *h = ctx->h;

    struct aggregate *ag = calloc (1, sizeof (*ag));
    if (ag == NULL)
        return NULL;

    ag->ctx = ctx;
    if (!(ag->key = strdup (key)) || !(ag->entries = zlist_new ())) {
        flux_log_error (h, "aggregate_create: memory allocation error");
        aggregate_destroy (ag);
        return (NULL);
    }
    return (ag);
}

static void aggregator_destroy (struct aggregator *ctx)
{
    if (ctx) {
        zhash_destroy (&ctx->aggregates);
        free (ctx);
    }
}

static int attr_get_int (flux_t *h, const char *attr)
{
    unsigned long l;
    char *p;
    const char *s = flux_attr_get (h, attr, 0);
    if (!s)
        return (-1);
    errno = 0;
    l = strtoul (s, &p, 10);
    if (*p != '\0' || errno != 0) {
        flux_log_error (h, "flux_attr_get (%s) = %s", attr, s);
        return (-1);
    }
    return (l);
}

static double timer_scale (flux_t *h)
{
    long level, maxlevel;
    if (((level = attr_get_int (h, "tbon.level")) < 0) ||
        ((maxlevel = attr_get_int (h, "tbon.maxlevel")) < 0)) {
        return (1.);
    }
    return (maxlevel - level + 1.);
}

static struct aggregator * aggregator_create (flux_t *h)
{
    struct aggregator * ctx = calloc (1, sizeof (*ctx));
    if (ctx == NULL)
        return (NULL);
    ctx->h = h;
    if (flux_get_rank (h, &ctx->rank) < 0) {
        flux_log_error (h, "flux_get_rank");
        goto error;
    }
    ctx->default_timeout = 0.01;
    ctx->timer_scale = timer_scale (h);
    if (!(ctx->aggregates = zhash_new ())) {
        flux_log_error (h, "zhash_new");
        goto error;
    }
    return (ctx);
error:
    aggregator_destroy (ctx);
    return (NULL);
}

/*
 *  Add a new aggregate to aggregator `ctx`. Insert into entries
 *   hash and set default minimum and maximum. Start the aggregate
 *   timeout, scaled by the current aggregator timeout scale.
 */
static struct aggregate *
aggregator_new_aggregate (struct aggregator *ctx, const char *key,
                          double timeout)
{
    struct aggregate *ag = aggregate_create (ctx, key);
    if (ag == NULL)
        return (NULL);

    if (zhash_insert (ctx->aggregates, key, ag) < 0) {
        aggregate_destroy (ag);
        return (NULL);
    }
    zhash_freefn (ctx->aggregates, key, (zhash_free_fn *) aggregate_destroy);
    ag->min = LLONG_MAX;
    ag->max = LLONG_MIN;
    ag->timeout = timeout;
    aggregate_timer_start (ctx, ag, timeout * ctx->timer_scale);
    return (ag);
}


/*
 *  Callback for "aggregator.push"
 */
static void push_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    int rc = -1;
    struct aggregator *ctx = arg;
    struct aggregate *ag = NULL;
    const char *json_str;
    json_object *in = NULL;
    const char *key;
    double timeout = ctx->default_timeout;
    int64_t fwd_count = 0;
    int saved_errno = 0;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = EPROTO;
        flux_log_error (h, "push: request decode");
        goto done;
    }
    if (!json_str || !(in = Jfromstr (json_str))) {
        saved_errno = EPROTO;
        flux_log_error (h, "push: json decode");
        goto done;
    }
    if (!(Jget_str (in, "key", &key))) {
        saved_errno = EPROTO;
        flux_log_error (h, "push: key missing");
        goto done;
    }
    // Allow request to override default aggregate timeout
    Jget_double (in,  "timeout", &timeout);
    Jget_int64 (in, "fwd_count", &fwd_count);

    if (!(ag = zhash_lookup (ctx->aggregates, key)) &&
        !(ag = aggregator_new_aggregate (ctx, key, timeout))) {
        flux_log_error (ctx->h, "failed to get new aggregate");
        saved_errno = errno;
        goto done;
    }

    if (fwd_count > 0)
        ag->fwd_count = fwd_count;

    if ((rc = aggregate_push_json (ag, in)) < 0)
        goto done;

    flux_log (ctx->h, LOG_INFO, "push: %s: count=%d fwd_count=%d total=%d",
                      ag->key, ag->count, ag->fwd_count, ag->total);
    if (ctx->rank > 0) {
        if ((ag->count == ag->total
            || ag->count == ag->fwd_count
            || timeout == 0.)
            && (rc = aggregate_flush (ag)))
            goto done;
    }
    else if (ag->count == ag->total)
        aggregate_try_sink (h, ag);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "aggregator.push: flux_respond");
    Jput (in);
}


static struct flux_msg_handler_spec htab[] = {
    //{ FLUX_MSGTYPE_EVENT,      "hb",               hb_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST,   "aggregator.push",  push_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    struct aggregator *ctx = aggregator_create (h);
    if (!ctx)
        goto done;

    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_advec");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_delvec;
    }
    rc = 0;
done_delvec:
    flux_msg_handler_delvec (htab);
done:
    aggregator_destroy (ctx);
    return rc;
}

MOD_NAME ("aggregator");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
