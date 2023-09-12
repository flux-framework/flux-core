/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _CONTENT_MMAP_H
#define _CONTENT_MMAP_H 1

#include <stdbool.h>

#include "cache.h"

struct content_mmap *content_mmap_create (flux_t *h,
                                          const char *hash_name,
                                          int hash_size);
void content_mmap_destroy (struct content_mmap *mm);

struct content_region *content_mmap_region_lookup (struct content_mmap *mm,
                                                   const void *hash,
                                                   int hash_size,
                                                   const void **data,
                                                   int *data_size);

bool content_mmap_validate (struct content_region *reg,
                            const void *hash,
                            int hash_size,
                            const void *data,
                            int data_size);

void content_mmap_region_decref (struct content_region *reg);
struct content_region *content_mmap_region_incref (struct content_region *reg);

#endif /* !_CONTENT_MMAP_H */

// vi:ts=4 sw=4 expandtab
