/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_BROKER_CONTENT_CACHE_H
#define HAVE_BROKER_CONTENT_CACHE_H 1

struct content_cache *content_cache_create (flux_t *h);
void content_cache_destroy (struct content_cache *cache);

int content_cache_register_attrs (struct content_cache *cache, attr_t *attr);

#endif /* !HAVE_BROKER_CONTENT_CACHE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
