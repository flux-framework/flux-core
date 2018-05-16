#ifndef _IDSET_H
#define _IDSET_H

#include <sys/types.h>

/* An idset is a numerically sorted set of non-negative integers  (0,1,2,3...).
 */

enum idset_flags {
    IDSET_FLAG_AUTOGROW = 1, // allow idset size to automatically grow
    IDSET_FLAG_BRACKETS = 2, // encode non-singleton idset with brackets
    IDSET_FLAG_RANGE = 4,    // encode with ranges ("2,3,4,8" -> "2-4,8")
};

/* Create/destroy an idset.
 * Set the initial size to 'size' (max id is size - 1)
 * 'flags' may include IDSET_FLAG_AUTOGROW.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_create (size_t size, int flags);
void idset_destroy (struct idset *idset);

/* Encode 'idset' to a string, which the caller must free.
 * 'flags' may include IDSET_FLAG_BRACKETS, IDSET_FLAG_RANGE.
 * Returns string on success, or NULL on failure with errno set.
 */
char *idset_encode (const struct idset *idset, int flags);

/* Decode string 's' to an idset.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_decode (const char *s);

#endif /* !_IDSET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
