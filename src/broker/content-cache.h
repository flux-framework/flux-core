/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

typedef struct content_cache content_cache_t;

int content_cache_set_flux (content_cache_t *cache, flux_t *h);

content_cache_t *content_cache_create (void);
void content_cache_destroy (content_cache_t *cache);

int content_cache_register_attrs (content_cache_t *cache, attr_t *attr);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
