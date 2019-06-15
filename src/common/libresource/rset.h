/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_RSET_H
#define HAVE_JOB_EXEC_RSET_H 1
#include <flux/idset.h>
#include <jansson.h>

struct resource_set;

struct resource_set *resource_set_create (const char *R, json_error_t *errp);

void resource_set_destroy (struct resource_set *rset);

const struct idset * resource_set_ranks (struct resource_set *rset);

double resource_set_starttime (struct resource_set *rset);

double resource_set_expiration (struct resource_set *rset);

#endif /* !HAVE_JOB_EXEC_RSET_H */


