/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PMIX_PP_MAP_H
#define _PMIX_PP_MAP_H

#include <jansson.h>

char *pp_map_node_create (json_t *R);
char *pp_map_proc_create (int nnodes, rcalc_t *rcalc);
char *pp_map_local_peers (int shell_rank, rcalc_t *rcalc);

#endif

// vi:ts=4 sw=4 expandtab
