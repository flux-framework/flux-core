/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PMIX_PP_CODEC
#define _PMIX_PP_CODEC

#include <pmix.h>
#include <jansson.h>

struct infovec *pp_infovec_create (void);
struct infovec *pp_infovec_create_from_json (json_t *o);
void pp_infovec_destroy (struct infovec *iv);

int pp_infovec_set_u32 (struct infovec *iv, const char *key, uint32_t val);
int pp_infovec_set_str (struct infovec *iv, const char *key, const char *str);
int pp_infovec_set_bool (struct infovec *iv, const char *key, bool val);
int pp_infovec_set_rank (struct infovec *iv, const char *key, pmix_rank_t val);

int pp_infovec_count (struct infovec *iv);
pmix_info_t *pp_infovec_info (struct infovec *iv);

#endif

// vi:ts=4 sw=4 expandtab
