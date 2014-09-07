#ifndef _UTIL_XZMALLOC_H
#define _UTIL_XZMALLOC_H

/* Memory allocation functions that call oom() on allocation error.
 */
void *xzmalloc (size_t size);
char *xstrdup (const char *s);

#endif /* !_UTIL_XZMALLOC_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
