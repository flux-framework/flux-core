#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#if !_UTIL_STRLCPY_H
#define _UTIL_STRLCPY_H

size_t strlcpy(char *dst, const char *src, size_t siz);
/*
 *  Copy src to string dst of size siz.  At most siz-1 characters
 *    will be copied.  Always NUL terminates (unless siz == 0).
 *  Returns strlen(src); if retval >= siz, truncation occurred.
 */
#endif /* !_UTIL_STRLCPY_H */
