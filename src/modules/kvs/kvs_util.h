#ifndef _FLUX_KVS_UTIL_H
#define _FLUX_KVS_UTIL_H

/* Normalize a KVS key
 * Returns new key string (caller must free), or NULL with errno set.
 * On success, 'want_directory' is set to true if key had a trailing
 * path separator.
 */
char *kvs_util_normalize_key (const char *key, bool *want_directory);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
