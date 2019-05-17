/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <stdarg.h>

#include <jansson.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libaggregate/aggregate.h"

struct aggregate_args {
    flux_t *h;
    uint32_t size;
    uint32_t rank;
    const char *key;
    json_t *o;
    double timeout;
    int fwd_count;
    bool verbose;
    struct timespec t0;
};

static const char usage[] = "[OPTIONS] KEY [JSON VALUE]";
static const char doc[] =
    "\n\
Front end test utility for creating \"aggregate\" JSON objects in the kvs. \
Must be run across all ranks, i.e. as `flux exec flux aggregate ...`. \
If JSON_VALUE is not supplied on the command line, reads value from stdin.\n\
";

static struct optparse_option opts[] =
    {{.name = "timeout",
      .key = 't',
      .arginfo = "T",
      .has_arg = 1,
      .usage = "Set reduction timeout to T seconds."},
     {.name = "fwd-count",
      .key = 'c',
      .arginfo = "N",
      .has_arg = 1,
      .usage = "Forward aggregate upstream after N"},
     {.name = "verbose",
      .key = 'v',
      .has_arg = 0,
      .usage = "Verbose operation"},
     OPTPARSE_TABLE_END};

static void verbose (struct aggregate_args *args, const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    if (!args->verbose)
        return;
    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    fprintf (stderr,
             "flux-aggregate: %.3fs: %s\n",
             monotime_since (args->t0) / 1000.,
             buf);
    va_end (ap);
}

json_t *json_from_stdin (void)
{
    json_error_t e;
    json_t *o = json_loadf (stdin, JSON_DECODE_ANY, &e);
    if (o == NULL)
        log_msg_exit ("Failed to decode JSON: %s", e.text);
    return (o);
}

json_t *json_from_string (const char *s)
{
    json_error_t e;
    json_t *o = json_loads (s, JSON_DECODE_ANY, &e);
    if (o == NULL)
        log_msg_exit ("Failed to decode JSON: %s", e.text);
    return (o);
}

static void unlink_aggregate_key (struct aggregate_args *args)
{
    const char *key = args->key;
    flux_t *h = args->h;
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn = NULL;

    /*
     * XXX: Special case: allow key of '.' to drop through for
     *  testing purposes
     */
    if (key[0] == '.' && key[1] == '\0')
        return;

    verbose (args, "unlinking %s", key);

    if (!(txn = flux_kvs_txn_create ()))
        log_err_exit ("flux_kvs_txn_create");
    if (flux_kvs_txn_unlink (txn, 0, key) < 0)
        log_err_exit ("flux_kvs_txn_unlink");
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        log_err_exit ("flux_kvs_commit");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("kvs commit rpc");

    verbose (args, "unlink complete");

    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
}

static void abort_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct aggregate_args *args = arg;
    fprintf (stderr,
             "flux-aggregate: %.3fs: aggregate aborted\n",
             monotime_since (args->t0) / 1000.);
    exit (1);
}

static flux_msg_handler_t *abort_msg_handler_create (struct aggregate_args *arg)
{
    int n;
    char buf[1024];
    struct flux_match match = FLUX_MATCH_EVENT;
    flux_msg_handler_t *mh = NULL;

    if ((n = snprintf (buf, sizeof (buf), "aggregator.abort.%s", arg->key) < 0)
        || (n >= sizeof (buf)))
        log_err_exit ("creating event name for key=%s", arg->key);

    match.topic_glob = buf;
    if (!(mh = flux_msg_handler_create (arg->h, match, abort_cb, (void *)arg)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (mh);

    if (flux_event_subscribe (arg->h, "aggregator.abort") < 0)
        log_err_exit ("flux_event_subscribe");
    verbose (arg, "subscribed to event %s", buf);
    return (mh);
}

void print_entries (json_t *entries)
{
    const char *key;
    json_t *value;
    json_object_foreach (entries, key, value) {
        char *s = json_dumps (value, JSON_ENCODE_ANY | JSON_COMPACT);
        printf ("%s: %s\n", key, s);
        free (s);
    }
}

void print_result (flux_future_t *f, void *arg)
{
    json_t *entries;
    if (aggregate_wait_get_unpack (f, "{s:o}", "entries", &entries) < 0)
        log_err_exit ("aggregate_wait_unpack");
    print_entries (entries);
    flux_reactor_stop (flux_future_get_reactor (f));
    flux_future_destroy (f);
}

static void aggregate_push_continue (flux_future_t *f, void *arg)
{
    flux_future_t *f2 = NULL;
    struct aggregate_args *args = arg;

    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("aggregate.push");
    if (args->rank != 0) {
        flux_future_destroy (f);
        flux_reactor_stop (flux_get_reactor (args->h));
        return;
    }
    verbose (args, "waiting for aggregate to complete");
    if (!(f2 = aggregate_wait (args->h, args->key)))
        log_err_exit ("aggregate_wait");
    if (flux_future_then (f2, 5., print_result, args) < 0)
        log_err_exit ("aggregate_wait");

    flux_future_destroy (f);
}

static void barrier_continue (flux_future_t *f, void *arg)
{
    struct aggregate_args *args = arg;
    flux_future_t *f2 = NULL;
    verbose (args, "barrier complete, calling aggregate.push");
    if (!(f2 = aggregator_push_json (args->h,
                                     args->fwd_count,
                                     args->timeout,
                                     args->key,
                                     args->o))
        || (flux_future_then (f2, -1., aggregate_push_continue, arg) < 0))
        log_err_exit ("aggregator_push_json");
    flux_future_destroy (f);
}

int main (int argc, char *argv[])
{
    struct aggregate_args args;
    int optindex;
    optparse_t *p = NULL;
    flux_msg_handler_t *mh = NULL;
    flux_future_t *f = NULL;

    memset (&args, 0, sizeof (args));
    monotime (&args.t0);

    if (!(p = optparse_create ("flux-aggregate")))
        log_msg_exit ("optparse_create");
    if (optparse_set (p, OPTPARSE_USAGE, usage) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage");
    if (optparse_add_doc (p, doc, 0) < 0)
        log_msg_exit ("optparse_add_doc");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);
    if ((argc - optindex) == 0) {
        optparse_print_usage (p);
        exit (1);
    }

    args.verbose = optparse_hasopt (p, "verbose");
    args.fwd_count = optparse_get_int (p, "fwd-count", 0);
    args.timeout = optparse_get_duration (p, "timeout", -1.);

    if (!(args.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (flux_get_rank (args.h, &args.rank) < 0)
        log_err_exit ("flux_get_rank");
    if (flux_get_size (args.h, &args.size) < 0)
        log_err_exit ("flux_get_size");

    /*  Only print messages from rank 0 on verbose operation */
    args.verbose = (args.rank == 0 && args.verbose);

    args.key = argv[optindex];
    if ((argc - optindex) == 1)  // read from stdin
        args.o = json_from_stdin ();
    else
        args.o = json_from_string (argv[optindex + 1]);

    verbose (&args, "starting aggregate on %d ranks", args.size);

    if (args.rank == 0) {
        unlink_aggregate_key (&args);
        mh = abort_msg_handler_create (&args);
    }
    if (!(f = flux_barrier (args.h, args.key, args.size))
        || (flux_future_then (f, -1., barrier_continue, &args)))
        log_err_exit ("flux_barrier");

    verbose (&args, "starting reactor");

    if (flux_reactor_run (flux_get_reactor (args.h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    verbose (&args, "all done");

    flux_msg_handler_destroy (mh);
    flux_close (args.h);
    optparse_destroy (p);
    log_fini ();
    return (0);
}

/* vi: ts=4 sw=4 expandtab
 */
