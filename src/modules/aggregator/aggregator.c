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
#include <jansson.h>

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
    int sink_retries;        /* number of times left to try to sink to kvs   */
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

/*  Push JSON object of aggregate entries onto aggregate `ag`
 */
static int aggregate_push_json (struct aggregate *ag, json_t *entries)
{
    const char *ids;
    json_t *val;
    json_object_foreach (entries, ids, val) {
        int64_t v = json_integer_value (val);
        if (aggregate_push (ag, v, ids) < 0) {
            flux_log_error (ag->ctx->h, "aggregate_push failed");
            return (-1);
        }
    }
    return (0);
}

/*  Return json object containing all "entries" from the current
 *   aggregate object `ag`
 */
static json_t *aggregate_entries_tojson (struct aggregate *ag)
{
    struct aggregate_entry *ae;
    json_t *o = NULL;
    json_t *entries = NULL;

    if (!(entries = json_object ()))
        return NULL;

    ae = zlist_first (ag->entries);
    while (ae) {
        if (!(o = json_integer (ae->value))
          || (json_object_set_new (entries, nodeset_string (ae->ids), o) < 0))
            goto error;
        ae = zlist_next (ag->entries);
    }
    return (entries);
error:
    json_decref (o);
    json_decref (entries);
    return (NULL);
}

static void forward_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct aggregate *ag = arg;
    if (flux_rpc_get (f, NULL) < 0)
        flux_log_error (h, "aggregator.push: key=%s", ag->key);
    flux_future_destroy (f);
}

/*
 *  Forward aggregate `ag` upstream
 */
static int aggregate_forward (flux_t *h, struct aggregate *ag)
{
    int rc = 0;
    flux_future_t *f;
    json_t *o = aggregate_entries_tojson (ag);

    if (o == NULL) {
        flux_log (h, LOG_ERR, "forward: aggregate_entries_tojson failed");
        return (-1);
    }
    flux_log (h, LOG_DEBUG, "forward: %s: count=%d total=%d",
                 ag->key, ag->count, ag->total);
    if (!(f = flux_rpc_pack (h, "aggregator.push", FLUX_NODEID_UPSTREAM, 0,
                                "{s:s,s:i,s:i,s:f,s:I,s:I,s:o}",
                                "key", ag->key,
                                "count", ag->count,
                                "total", ag->total,
                                "timeout", ag->timeout,
                                "min", ag->min,
                                "max", ag->max,
                                "entries", o)) ||
        (flux_future_then (f, -1., forward_continuation, (void *) ag) < 0)) {
        flux_log_error (h, "flux_rpc: aggregator.push");
        flux_future_destroy (f);
        rc = -1;
    }
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

static void aggregate_sink (flux_t *h, struct aggregate *ag);

static void aggregate_sink_again (flux_reactor_t *r, flux_watcher_t *w,
                                 int revents, void *arg)
{
    struct aggregate *ag = arg;
    aggregate_sink (ag->ctx->h, ag);
    flux_watcher_destroy (w);
}

static int sink_retry (flux_t *h, struct aggregate *ag)
{
    flux_watcher_t *w;
    double t = ag->timeout;
    if (t <= 1e-3)
        t = .250;

    /* Return with error if we're out of retries */
    if (--ag->sink_retries <= 0)
        return (-1);

    flux_log (h, LOG_DEBUG, "sink: %s: retry  in %.3fs", ag->key, t);
    w = flux_timer_watcher_create (flux_get_reactor (h),
                                   t, 0.,
                                   aggregate_sink_again,
                                   (void *) ag);
    if (w == NULL) {
        flux_log_error (h, "sink_retry: flux_timer_watcher_create");
        return (-1);
    }
    flux_watcher_start (w);
    return (0);
}

static void sink_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct aggregate *ag = arg;

    int rc = flux_future_get (f, NULL);
    flux_future_destroy (f);
    if (rc < 0) {
        /*  Schedule a retry, if  succesful return immediately, otherwise
         *   abort the current aggregate and remove it.
         */
        if (sink_retry (h, ag) == 0)
            return;
        aggregate_sink_abort (h, ag);
    }
    zhash_delete (ag->ctx->aggregates, ag->key);
    return;
}

static void  aggregate_sink (flux_t *h, struct aggregate *ag)
{
    int rc = -1;
    json_t *o = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    flux_log (h, LOG_DEBUG, "sink: %s: count=%d total=%d",
                ag->key, ag->count, ag->total);

    /* Fail on key == "." */
    if (strcmp (ag->key, ".") == 0) {
        flux_log (h, LOG_ERR, "sink: refusing to sink to rootdir");
        goto out;
    }
    if (!(o = aggregate_entries_tojson (ag))) {
        flux_log (h, LOG_ERR, "sink: aggregate_entries_tojson failed");
        goto out;
    }
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "sink: flux_kvs_txn_create");
        goto out;
    }
    if (flux_kvs_txn_pack (txn, 0, ag->key,
                            "{s:i,s:i,s:I,s:I,s:o}",
                            "total", ag->total,
                            "count", ag->count,
                            "max", ag->max,
                            "min", ag->min,
                            "entries", o) < 0) {
        flux_log_error (h, "sink: flux_kvs_txn_put");
        goto out;
    }
    o = NULL;
    if (!(f = flux_kvs_commit (h, 0, txn))
        || flux_future_then (f, -1., sink_continuation, (void *)ag) < 0) {
        flux_log_error (h, "sink: flux_kvs_commit");
        flux_future_destroy (f);
        goto out;
    }
    rc = 0;
out:
    flux_kvs_txn_destroy (txn);
    json_decref (o);
    if ((rc < 0) && (sink_retry (h, ag) < 0)) {
        aggregate_sink_abort (h, ag);
        zhash_delete (ag->ctx->aggregates, ag->key);
    }
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
        if (ag->tw == NULL) {
            flux_log_error (h, "flux_timer_watcher_create");
            return;
        }
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
    ag->sink_retries = 2;
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
                          int64_t total,
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
    ag->total = total;
    ag->ctx = ctx;
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
    const char *key;
    double timeout = ctx->default_timeout;
    int64_t fwd_count = 0;
    int64_t total = 0;
    json_t *entries = NULL;
    int saved_errno = 0;

    if (flux_msg_unpack (msg, "{s:s,s:I,s:o,s?F,s?I}",
                              "key", &key,
                              "total", &total,
                              "entries", &entries,
                              "timeout", &timeout,
                              "fwd_count", &fwd_count) < 0) {
        saved_errno = EPROTO;
        goto done;
    }

    if (!(ag = zhash_lookup (ctx->aggregates, key)) &&
        !(ag = aggregator_new_aggregate (ctx, key, total, timeout))) {
        flux_log_error (ctx->h, "failed to get new aggregate");
        saved_errno = errno;
        goto done;
    }

    if (fwd_count > 0)
        ag->fwd_count = fwd_count;

    if ((rc = aggregate_push_json (ag, entries)) < 0) {
        flux_log_error (h, "aggregate_push_json: failed");
        goto done;
    }

    flux_log (ctx->h, LOG_DEBUG, "push: %s: count=%d fwd_count=%d total=%d",
                      ag->key, ag->count, ag->fwd_count, ag->total);
    if (ctx->rank > 0) {
        if ((ag->count == ag->total
            || ag->count == ag->fwd_count
            || timeout == 0.)
            && (rc = aggregate_flush (ag)))
            goto done;
    }
    else if (ag->count == ag->total)
        aggregate_sink (h, ag);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0, NULL) < 0)
        flux_log_error (h, "aggregator.push: flux_respond");
}


static const struct flux_msg_handler_spec htab[] = {
    //{ FLUX_MSGTYPE_EVENT,      "hb",               hb_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "aggregator.push",  push_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    flux_msg_handler_t **handlers = NULL;
    struct aggregator *ctx = aggregator_create (h);
    if (!ctx)
        goto done;

    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_advec");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    aggregator_destroy (ctx);
    return rc;
}

MOD_NAME ("aggregator");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
