#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#if !_MISSING_STRLCAT_H
#define _MISSING_STRLCAT_H

size_t strlcat(char *dst, const char *src, size_t siz);
/*
 *  Appends src to string dst of size siz (unlike strncat, siz is the
 *    full size of dst, not space left).  At most siz-1 characters
 *    will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 *  Returns strlen(src) + MIN(siz, strlen(initial dst)).
 *  If retval >= siz, truncation occurred.
 */

#endif // !_MISSING_STRLCAT
