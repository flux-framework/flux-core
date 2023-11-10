/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_WORKCREW_H
#define _JOB_INGEST_WORKCREW_H

#include <flux/core.h>

#include "types.h"

struct workcrew;

json_t *workcrew_stats_get (struct workcrew *crew);

/* Submit one job to workcrew.
 * Future is fulfilled once processing is complete.
 */
flux_future_t *workcrew_process_job (struct workcrew *crew, json_t *job);

/* Tell workcrew to stop.
 * Return a count of running processes.
 * If nonzero, arrange for callback to be called each time a process exits.
 */
int workcrew_stop_notify (struct workcrew *crew, process_exit_f cb, void *arg);

struct workcrew *workcrew_create (flux_t *h);

/* (Re-)configure work crew command.  This must be called initially and then
 * may be called again when the config changes.  Workers pick up changes on
 * the next restart.  The worker command line will be:
 *
 * flux <cmdname> [--plugins <plugins>] [<args>]
 *
 * plugins should be a comma-delimited list of plugin names, or NULL.
 * It is passed through as one command line argument with delimiters intact.
 *
 * args should be a comma-delimited list of additional arguments, or NULL.
 * The list is split into separate command line arguments.
 *
 * bufsize should be a string buffer size represented as a floating point
 * value with optional scale suffix [kKMG].
 */
int workcrew_configure (struct workcrew *crew,
                        const char *cmdname,
                        const char *plugins,
                        const char *args,
                        const char *bufsize);

void workcrew_destroy (struct workcrew *crew);

#endif /* !_JOB_INGEST_WORKCREW_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
