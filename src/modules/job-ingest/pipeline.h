/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_PIPELINE_H
#define _JOB_INGEST_PIPELINE_H

#include <jansson.h>
#include <flux/core.h>

#include "job.h"

struct pipeline *pipeline_create (flux_t *h);
void pipeline_destroy (struct pipeline *pl);

void pipeline_shutdown (struct pipeline *pl);

int pipeline_configure (struct pipeline *pl,
                        const flux_conf_t *conf,
                        int argc,
                        char **argv,
                        flux_error_t *error);

int pipeline_process_job (struct pipeline *pl,
                          struct job *job,
                          flux_future_t **fp,
                          flux_error_t *error);

json_t *pipeline_stats_get (struct pipeline *pl);

#endif /* !_JOB_INGEST_PIPELINE_H */

// vi:ts=4 sw=4 expandtab
