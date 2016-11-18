#ifndef _UTIL_BLOBREF_H
#define _UTIL_BLOBREF_H

#define BLOBREF_MAX_STRING_SIZE     72
#define BLOBREF_MAX_DIGEST_SIZE     32

#include <stdint.h>

/* Convert a blobref string to hash digest.
 * The hash algorithm is selected by the blobref prefix.
 * Returns hash length on success, or -1 on error, with errno set.
 */
int blobref_strtohash (const char *s, void *hash, int size);

/* Convert a hash digest to null-terminated blobref string in 's'.
 * The hash algorithm is selected by 'hashtype', e.g. "sha1".
 * Returns 0 on success, -1 on error, with errno set.
 */
int blobref_hashtostr (const char *hashtype,
                       const void *hash, int len,
                       char *s, int size);

/* Compute hash over data and return null-terminated blobref string in 's'.
 * The hash algorithm is selected by 'hashtype', e.g. "sha1".
 * Returns 0 on success, -1 on error with errno set.
 */
int blobref_hash (const char *hashtype,
                  const void *data, int len,
                  char *s, int size);

/* Check validity of blobref string.
 */
int blobref_validate (const char *blobref);

#endif /* _UTIL_BLOBREF_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
