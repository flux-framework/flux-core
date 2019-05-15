/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_UTIL_H
#define _FLUX_KVS_UTIL_H

/* Normalize a KVS key
 * Returns new key string (caller must free), or NULL with errno set.
 * On success, 'want_directory' is set to true if key had a trailing
 * path separator.
 */
char *kvs_util_normalize_key (const char *key, bool *want_directory);

/* Get kvs namespace from FLUX_KVS_NAMESPACE environment variable, or
 * if not set, return default */
const char *kvs_get_namespace (void);

#endif /* !_FLUX_KVS_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
