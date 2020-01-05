/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SHELL_MPIR_RANGELIST_H
#define HAVE_SHELL_MPIR_RANGELIST_H 1

#include <stdint.h>
#include <jansson.h>

struct rangelist;

#define RANGELIST_END INT64_MIN

struct rangelist *rangelist_create (void);
void rangelist_destroy (struct rangelist *rl);

int rangelist_append (struct rangelist *rl, int64_t n);
int rangelist_append_list (struct rangelist *rl, struct rangelist *new);

int64_t rangelist_size (struct rangelist *rl);

int64_t rangelist_first (struct rangelist *rl);
int64_t rangelist_next (struct rangelist *rl);

json_t *rangelist_to_json (struct rangelist *rl);
struct rangelist *rangelist_from_json (json_t *o);

#endif /* !HAVE_SHELL_MPIR_RANGELIST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
