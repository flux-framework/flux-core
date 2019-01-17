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

struct validate *v;

/* Submit jobspec ('buf, 'len') for validation.
 * Future is fulfilled once validation is complete.
 */
flux_future_t *validate_jobspec (struct validate *v, const char *buf, int len);

struct validate *validate_create (flux_t *h);

void validate_destroy (struct validate *v);

#endif /* !_JOB_INGEST_VALIDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
