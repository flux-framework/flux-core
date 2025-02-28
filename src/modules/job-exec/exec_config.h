/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_CONFIG_H
#define HAVE_JOB_EXEC_CONFIG_H 1

#include <jansson.h>
#include <stdbool.h>
#include <flux/core.h>

#include "job-exec.h"

/* Configuration getters.  It is not safe to hold on to returned
 * values.  Callers should strdup() / json_incref() / etc. values
 * they wish to access for later use.
 */

const char *config_get_job_shell (struct jobinfo *job);

const char *config_get_cwd (struct jobinfo *job);

const char *config_get_imp_path (void);

const char *config_get_exec_service (void);

json_t *config_get_sdexec_properties (void);

bool config_get_exec_service_override (void);

double config_get_default_barrier_timeout (void);

int config_get_stats (json_t **config_stats);

const char *config_get_sdexec_stop_timer_sec (void);

const char *config_get_sdexec_stop_timer_signal (void);

int config_setup (flux_t *h,
                  const flux_conf_t *conf,
                  int argc,
                  char **argv,
                  flux_error_t *errp);

#endif /* !HAVE_JOB_EXEC_CONFIG_EXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
