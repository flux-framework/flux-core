/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_RLIST_MATCH_H
#define HAVE_RLIST_MATCH_H 1

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h> /* flux_error_t */

#include "rnode.h"

/*  Return true if rnode 'n' matches constraints in RFC 31 constraint
 *   specification 'constraint'.
 */
bool rnode_match (const struct rnode *n, json_t *constraint);

/*  Validate RFC 31 constraint spec 'constraint'
 *
 *  Returns 0 if constraint is valid spec,
 *         -1 if not and sets error in errp if errp != NULL.
 *
 */
int rnode_match_validate (json_t *constraint, flux_error_t *errp);

/*  Copy an rnode only if it matches the RFC 31 constraints in `constraint` */
struct rnode *rnode_copy_match (const struct rnode *n, json_t *constraint);

#endif /* !HAVE_SCHED_RLIST_MATCH */
