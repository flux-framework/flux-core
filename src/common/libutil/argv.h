#ifndef _UTIL_ARGV_H
#define _UTIL_ARGV_H

#include <stdarg.h>

/* Return a string with argcv elements delimited by 'sep'.  Caller must free.
 */
char *argv_concat (int argc, char *argv[], const char *sep);

/* Create a new, empty but NULL-termianted argv array.
 * argc will be set to 0.
 */
void argv_create (int *argcp, char ***argvp);

/* Destroy a NULL-terminated argv array.
 * Each argument will be freed, assuming it was pushed on with
 * argv_push(), which makes a copy.
 */
void argv_destroy (int argc, char *argv[]);

/* Push arg onto argv array, making a copy of arg.
 */
void argv_push (int *argcp, char ***argvp, const char *fmt, ...);

#endif /* !_UTIL_ARGV_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
