/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _OVERLAY_BOOTSTRAP_H
#define _OVERLAY_BOOTSTRAP_H

#include <flux/core.h>
#include <flux/idset.h>
#include "src/common/libpmi/bizcard.h"

int boot_util_iam (flux_t *h, const struct bizcard *bc, flux_error_t *errp);

int boot_util_barrier (flux_t *h, flux_error_t *errp);

struct bizcard *boot_util_whois_rank (flux_t *h, int rank, flux_error_t *errp);

flux_future_t *boot_util_whois (flux_t *h,
                                int *ranks,
                                int count,
                                flux_error_t *errp);

struct bizcard *boot_util_whois_get_bizcard (flux_future_t *f);

int boot_util_whois_get_rank (flux_future_t *f);

int boot_util_finalize (flux_t *h,
                        struct idset *critical_ranks,
                        flux_error_t *errp);

#endif /* !_OVERLAY_BOOTSTRAP_H */

// vi:ts=4 sw=4 expandtab
