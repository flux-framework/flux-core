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
#    define _JOB_INGEST_WORKER_H

#    include <flux/core.h>

struct worker;

flux_future_t *worker_request (struct worker *w, const char *s);

int worker_queue_depth (struct worker *w);
bool worker_is_running (struct worker *w);

void worker_destroy (struct worker *w);
struct worker *worker_create (flux_t *h,
                              double inactivity_timeout,
                              int argc,
                              char **argv);

#endif /* !_JOB_INGEST_VALIDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
