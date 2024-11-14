/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_WORKER_H
#define _JOB_INGEST_WORKER_H

#include <sys/types.h>
#include <flux/core.h>

#include "types.h"

struct worker;


flux_future_t *worker_request (struct worker *w, const char *s);

int worker_queue_depth (struct worker *w);
int worker_request_count (struct worker *w);
int worker_error_count (struct worker *w);
int worker_trash_count (struct worker *w);
bool worker_is_running (struct worker *w);
pid_t worker_pid (struct worker *w);

flux_future_t *worker_kill (struct worker *w, int signo);
void worker_destroy (struct worker *w);

struct worker *worker_create (flux_t *h,
                              double inactivity_timeout,
                              const char *worker_name);

/*  (re)set cmdline for worker `w`. The new command will be used the
 *   next time the worker starts.
 */
int worker_set_cmdline (struct worker *w, int argc, char **argv);

/*  (re)set stdin buffer size for worker `w`.
 *  `bufsize` may be a floating point value with optional scale suffix:
 *  'kKMG'
 */
int worker_set_bufsize (struct worker *w, const char *bufsize);

/* Tell worker to stop.
 * Return a count of running processes.
 * If nonzero, arrange for callback to be called each time a process exits.
 */
int worker_stop_notify (struct worker *w, process_exit_f cb, void *arg);

#endif /* !_JOB_INGEST_VALIDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
