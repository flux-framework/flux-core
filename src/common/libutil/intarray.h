#ifndef _UTIL_INTARRAY_H
#define _UTIL_INTARRAY_H

/* Parse the string 's' into an array of integers, setting 'ia'
 * to a malloc()ed array and 'ia_count' to its length.
 * The string should contain integers delimited by commas.
 * Returns 0 on success, -1 on failure with errno set.
 */
int intarray_create (const char *s, int **ia, int *ia_count);

#endif /* !_UTIL_INTARRAY_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
