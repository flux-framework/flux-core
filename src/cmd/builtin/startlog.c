/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <unistd.h>
#include <stdarg.h>
#include <jansson.h>
#include <time.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libkvs/treeobj.h"
#include "src/common/libkvs/kvs_checkpoint.h"
#include "src/common/libutil/fsd.h"
#include "ccan/str/str.h"

#include "builtin.h"

#define DEFAULT_STARTLOG_KEY "admin.eventlog"
#define DEFAULT_STARTLOG_VERSION 1

static const char *startlog_key;
static int startlog_version;

enum post_flags {
    POST_FLAG_FLUSH = 1,
};

static void post_startlog_event (flux_t *h,
                                 enum post_flags flags,
                                 const char *name,
                                 const char *fmt, ...)
{
    va_list ap;
    json_t *o;
    char *event;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    uint32_t rank;
    int commit_flags = 0;

    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("Error fetching rank");
    if (rank > 0)
        log_msg_exit ("Startlog events may only be posted from rank 0");

    va_start (ap, fmt);
    o = eventlog_entry_vpack (0., name, fmt, ap);
    va_end (ap);

    if (!o
        || !(event = eventlog_entry_encode (o))
        || !(txn = flux_kvs_txn_create ())
        || flux_kvs_txn_put (txn,
                             FLUX_KVS_APPEND,
                             startlog_key,
                             event) < 0)
        log_err_exit ("Error creating %s event", name);

    if (flags == POST_FLAG_FLUSH)
        commit_flags |= FLUX_KVS_SYNC;

    if (!(f = flux_kvs_commit (h, NULL, commit_flags, txn))
        || flux_rpc_get (f, NULL) < 0)
        log_msg_exit ("Error committing %s event: %s",
                      name,
                      future_strerror (f, errno));

    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (event);
    json_decref (o);
}

static int startlog_parse_event (json_t *entry,
                                 double *timestampp,
                                 const char **namep,
                                 json_t **contextp)
{
    double timestamp;
    const char *name;
    json_t *context;
    int version;

    if (eventlog_entry_parse (entry, &timestamp, &name, &context) < 0
        || json_unpack (context, "{s:i}", "version", &version) < 0
        || version < 0
        || version > startlog_version)
        return -1;
    if (timestampp)
        *timestampp = timestamp;
    if (namep)
        *namep = name;
    if (contextp)
        *contextp = context;
    return 0;
}

static void startlog_post_start_event (flux_t *h, optparse_t *p)
{
    post_startlog_event (h,
                         POST_FLAG_FLUSH,
                         "start",
                         "{s:i s:s}",
                         "version", startlog_version,
                         "core_version", flux_core_version_string ());
}

static void startlog_post_finish_event (flux_t *h, optparse_t *p)
{
    post_startlog_event (h,
                         0,
                         "finish",
                         "{s:i}",
                         "version", startlog_version);
}

static json_t *startlog_fetch (flux_t *h)
{
    flux_future_t *f;
    const char *raw;
    json_t *event_array;

    if (!(f = flux_kvs_lookup (h, NULL, 0, startlog_key))
        || flux_kvs_lookup_get (f, &raw) < 0)
        log_msg_exit ("Error fetching eventlog: %s",
                      future_strerror (f, errno));
    if (!(event_array = eventlog_decode (raw)))
        log_err_exit ("Error decoding eventlog");
    flux_future_destroy (f);
    return event_array;
}

static int format_timestamp (char *buf, size_t size, time_t t)
{
    struct tm tm;
    if (t < 0 || !localtime_r (&t, &tm))
        return -1;
    if (strftime (buf, size, "%Y-%m-%d %R", &tm) == 0)
        return -1;
    return 0;
}

/* Interpret startlog and list instance restart durations.
 */
static void startlog_list (flux_t *h, optparse_t *p)
{
    json_t *event_array = startlog_fetch (h);
    bool check = optparse_hasopt (p, "check");
    bool quiet = optparse_hasopt (p, "quiet");
    bool crashed = false;
    size_t index;
    json_t *entry;
    double timestamp;
    const char *name;
    json_t *context;
    char timebuf[64];
    bool started = false;
    double last_timestamp = 0;
    char fsd[64];

    json_array_foreach (event_array, index, entry) {
        if (startlog_parse_event (entry, &timestamp, &name, &context) < 0)
            continue; // ignore (but tolerate) non-conforming entries
        format_timestamp (timebuf, sizeof (timebuf), timestamp);
        if (streq (name, "start")) {
            if (started) {
                if (!quiet)
                    printf ("crashed\n");
                crashed = true;
            }
            if (!quiet) {
                const char *s = "";
                if (optparse_hasopt (p, "show-version")) {
                    (void)json_unpack (context, "{s:s}", "core_version", &s);
                    printf ("%25s  ", s);
                }
                printf ("%s - ", timebuf);
            }
            started = true;
        }
        else if (streq (name, "finish")) {
            if (started) {
                fsd_format_duration_ex (fsd,
                                        sizeof (fsd),
                                        timestamp - last_timestamp,
                                        2);
                if (!quiet)
                    printf ("%s (%s)\n", timebuf, fsd);
                crashed = false;
            }
            started = false;
        }
        last_timestamp = timestamp;
    }
    if (started) {
        flux_reactor_t *r = flux_get_reactor (h);
        fsd_format_duration_ex (fsd,
                                sizeof (fsd),
                                flux_reactor_now (r) - last_timestamp,
                                2);
        if (!quiet)
            printf ("running (%s)\n", fsd);
    }
    if (check && crashed)
        exit (1);
}

static int cmd_startlog (optparse_t *p, int ac, char *av[])
{
    flux_t *h = builtin_get_flux_handle (p);

    startlog_key = optparse_get_str (p,
                                     "test-startlog-key",
                                     DEFAULT_STARTLOG_KEY);
    startlog_version = optparse_get_int (p,
                                         "test-startlog-version",
                                         DEFAULT_STARTLOG_VERSION);
    if (optparse_hasopt (p, "post-start-event"))
        startlog_post_start_event (h, p);
    else if (optparse_hasopt (p, "post-finish-event"))
        startlog_post_finish_event (h, p);
    else
        startlog_list (h, p);
    return 0;
}

static struct optparse_option startlog_opts[] = {
    { .name = "check", .has_arg = 0,
      .usage = "Check if instance was properly shut down",
    },
    { .name = "quiet", .has_arg = 0,
      .usage = "Suppress listing, useful with --check",
    },
    { .name = "show-version", .key = 'v', .has_arg = 0,
      .usage = "Show the flux-core version string in output",
    },
    { .name = "post-start-event", .has_arg = 0,
      .usage = "Post start event to eventlog (for rc use only)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    { .name = "post-finish-event", .has_arg = 0,
      .usage = "Post finish event to eventlog (for rc use only)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    { .name = "test-startlog-key", .has_arg = 1, .arginfo = "PATH",
      .usage = "override startlog key (test only)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    { .name = "test-startlog-version", .has_arg = 1, .arginfo = "VERSION",
      .usage = "override startlog version (test only)",
      .flags = OPTPARSE_OPT_HIDDEN,
    },
    OPTPARSE_TABLE_END
};

int subcommand_startlog_register (optparse_t *p)
{
    optparse_err_t e;
    e = optparse_reg_subcommand (p,
        "startlog",
        cmd_startlog,
        "[OPTIONS]",
        "List Flux instance startlog",
        0,
        startlog_opts);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
