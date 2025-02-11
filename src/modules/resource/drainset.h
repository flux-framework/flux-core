/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_DRAINSET_H
#define _FLUX_RESOURCE_DRAINSET_H

#include <stdbool.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/idset.h>

struct drainset * drainset_create (void);
void drainset_destroy (struct drainset *dset);

int drainset_drain_rank (struct drainset *dset,
                         unsigned int rank,
                         double timestamp,
                         const char *reason);

int drainset_drain_ex (struct drainset *dset,
                       unsigned int rank,
                       double timestamp,
                       const char *reason,
                       int overwrite);

int drainset_undrain (struct drainset *dset,
                      unsigned int rank);

json_t *drainset_to_json (struct drainset *dset);

struct drainset *drainset_from_json (json_t *o);

#endif /* ! _FLUX_RESOURCE_DRAINSET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
