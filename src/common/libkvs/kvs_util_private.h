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

/* Check if key contains a namespace within it with the "ns:X/" prefix
 * format, e.g.
 *
 * key_orig = "ns:foo/bar"
 *
 * Indicates a namespace of "foo" and the key "bar".  Returns 1 if
 * namespace prefix exists, 0 if not, -1 on error.  Returns prefix and
 * suffix in namespace_prefix and key_suffix appropriately.  Caller is
 * responsible for freeing memory from return pointers.
 */
int kvs_namespace_prefix (const char *key,
                          char **namespace_prefix,
                          char **key_suffix);

#endif  /* !_FLUX_KVS_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
