/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job hostpids */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/idset.h>

#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif

#include "src/common/libutil/log.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libdebugged/debugged.h"
#include "ccan/str/str.h"

#include "common.h"
#include "mpir.h"

typedef struct {
    char *host_name;
    char *executable_name;
    int pid;
} MPIR_PROCDESC;

extern int MPIR_proctable_size;
extern MPIR_PROCDESC *MPIR_proctable;

struct optparse_option hostpids_opts[] =  {
    { .name = "delimiter",
      .key = 'd',
      .has_arg = 1,
      .arginfo = "STRING",
      .usage = "Set output delimiter (default=\",\")",
    },
    { .name = "ranks",
      .key = 'r',
      .has_arg = 1,
      .arginfo = "IDSET",
      .usage = "Include only task ranks in IDSET",
    },
    { .name = "timeout",
      .key = 't',
      .has_arg = 1,
      .arginfo = "DURATION",
      .usage = "timeout after DURATION",
    },
    OPTPARSE_TABLE_END
};

struct hostpids_ctx {
    flux_t *h;
    flux_jobid_t id;
    optparse_t *opts;
    int leader_rank;
    char shell_service[128];
};

static void print_hostpids (const char *delim, struct idset *ranks)
{
    const char *delimiter = "";
    for (int i = 0; i < MPIR_proctable_size; i++) {
        if (ranks && !idset_test (ranks, i))
            continue;
        printf ("%s%s:%d",
                delimiter,
                MPIR_proctable[i].host_name,
                MPIR_proctable[i].pid);
        delimiter = delim;
    }
    printf ("\n");
}

static void mpir_setup (struct hostpids_ctx *ctx)
{
    mpir_setup_interface (ctx->h,
                          ctx->id,
                          false,
                          false,
                          ctx->leader_rank,
                          ctx->shell_service);
}

static void event_watch_cb (flux_future_t *f, void *arg)
{
    struct hostpids_ctx *ctx = arg;
    const char *entry;
    json_t *o = NULL;
    double timestamp;
    const char *name;
    json_t *context;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            goto done;
        if (errno == ETIMEDOUT)
            log_msg_exit ("hostpids: timeout waiting for shell.start event");
        if (errno == EPERM)
            log_msg_exit ("hostpids: Permission denied");
        log_msg_exit ("flux_job_event_watch_get: %s",
                      future_strerror (f, errno));
    }
    if (!(o = eventlog_entry_decode (entry)))
        log_err_exit ("eventlog_entry_decode");
    if (eventlog_entry_parse (o, &timestamp, &name, &context) < 0)
        log_err_exit ("eventlog_entry_parse");
    if (streq (name, "shell.init")) {
        const char *service;
        int len = sizeof (ctx->shell_service);
        if (json_unpack (context,
                         "{s:i s:s}",
                         "leader-rank", &ctx->leader_rank,
                         "service", &service) < 0)
            log_err_exit ("error decoding shell.init event");
        if (strlcpy (ctx->shell_service, service, len) >= len)
            log_msg_exit ("error caching shell service name");
    }
    else if (streq (name, "shell.start")) {
        /* Setup MPIR_procdesc and pop out of reactor to print result */
        mpir_setup (ctx);
        goto done;
    }
    json_decref (o);
    flux_future_reset (f);
    return;
done:
    json_decref (o);
    flux_future_destroy (f);
}

static void check_valid_jobid (struct hostpids_ctx *ctx, const char *jobid)
{
    flux_future_t *f;
    flux_job_state_t state;

    /*  Arrange for early exit if job is in INACTIVE or CLEANUP
     */
    if (!(f = flux_job_list_id (ctx->h, ctx->id, "[\"state\"]")))
        log_err_exit ("failed to issue job-list.list-id RPC");
    if (flux_rpc_get_unpack (f, "{s:{s:i}}", "job", "state", &state) < 0) {
        if (errno == ENOENT)
            log_msg_exit ("%s: No such job", jobid);
        log_err_exit ("job list failed for %s", jobid);
    }
    flux_future_destroy (f);
    if (!(state & FLUX_JOB_STATE_ACTIVE))
        log_msg_exit ("hostpids: job %s is inactive", jobid);
}

int cmd_hostpids (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    flux_future_t *f;
    struct hostpids_ctx ctx = { 0 };
    struct idset *task_ranks = NULL;
    idset_error_t error;
    const char *jobid;
    const char *delim = optparse_get_str (p, "delimiter", ",");
    const char *ranks = optparse_get_str (p, "ranks", "all");
    double timeout = optparse_get_duration (p, "timeout", -1.);

    if (streq (delim, "\\n"))
        delim = "\n";

    if (argc - optindex != 1) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!streq (ranks, "all")
        && !(task_ranks = idset_decode_ex (ranks, -1, -1, 0, &error)))
        log_err_exit ("--ranks=%s: %s", ranks, error.text);

    jobid = argv[optindex];
    ctx.opts = p;
    ctx.id = parse_jobid (jobid);
    if (!(ctx.h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    check_valid_jobid (&ctx, jobid);

    if (!(f = flux_job_event_watch (ctx.h,
                                    ctx.id,
                                    "guest.exec.eventlog",
                                    FLUX_JOB_EVENT_WATCH_WAITCREATE)))
        log_err_exit ("flux_job_event_watch");
    if (flux_future_then (f, timeout, event_watch_cb, &ctx) < 0)
        log_err_exit ("flux_future_then");

    if (flux_reactor_run (flux_get_reactor (ctx.h), 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (MPIR_proctable_size <= 0)
        log_msg_exit ("failed to get MPIR_proctable from job shell");

    print_hostpids (delim, task_ranks);
    idset_destroy (task_ranks);
    flux_close (ctx.h);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
