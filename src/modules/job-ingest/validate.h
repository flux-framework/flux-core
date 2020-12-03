/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_INGEST_VALIDATE_H
#define _JOB_INGEST_VALIDATE_H

#include <flux/core.h>

#include "types.h"

struct validate;

/* Submit jobspec for validation.
 * Future is fulfilled once validation is complete.
 */
flux_future_t *validate_jobspec (struct validate *v, json_t *jobspec);

/* Tell validators to stop.
 * Return a count of running processes.
 * If nonzero, arrange for callback to be called each time a process exits.
 */
int validate_stop_notify (struct validate *v, process_exit_f cb, void *arg);

struct validate *validate_create (flux_t *h,
                                  const char *validate_path,
                                  const char *validator_args);

void validate_destroy (struct validate *v);

#endif /* !_JOB_INGEST_VALIDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
