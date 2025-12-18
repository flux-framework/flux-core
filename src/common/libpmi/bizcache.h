/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBPMI_BIZCACHE_H
#define LIBPMI_BIZCACHE_H

#include <flux/core.h>
#include <jansson.h>

#include "upmi.h"
#include "bizcard.h"

struct bizcache *bizcache_create (struct upmi *upmi, size_t size);
void bizcache_destroy (struct bizcache *cache);

int bizcache_put (struct bizcache *cache,
                  int rank,
                  const struct bizcard *bc,
                  flux_error_t *error);
int bizcache_get (struct bizcache *cache,
                  int rank,
                  const struct bizcard **bcp,
                  flux_error_t *error);


#endif /* LIBPMI_BIZCACHE_H */

// vi:ts=4 sw=4 expandtab
