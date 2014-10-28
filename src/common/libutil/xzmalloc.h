#ifndef _UTIL_XZMALLOC_H
#define _UTIL_XZMALLOC_H

#include <sys/types.h>
#include <stdarg.h>

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
void *xrealloc (void *ptr, size_t size);
char *xstrdup (const char *s);
char *xvasprintf(const char *fmt, va_list ap);
char *xasprintf (const char *fmt, ...);
char *xstrsub (const char *str, char a, char b);

#endif /* !_UTIL_XZMALLOC_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

