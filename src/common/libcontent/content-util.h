/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* NOTE: these functions all log error messages to the broker.
 */

#ifndef _FLUX_CONTENT_UTIL_H
#define _FLUX_CONTENT_UTIL_H

/* Let the rank 0 content-cache service know the backing store is available.
 * This function blocks while waiting for the RPC response.
 */
int content_register_backing_store (flux_t *h, const char *name);

/* Let the rank 0 content-cache service know the backing store is not available.
 * This function blocks while waiting for the RPC response.
 */
int content_unregister_backing_store (flux_t *h);

/* Wrapper to synchronously register a flux service.
 * This function blocks while waiting for the RPC response.
 */
int content_register_service (flux_t *h, const char *name);

#endif /* !_FLUX_CONTENT_UTIL_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
