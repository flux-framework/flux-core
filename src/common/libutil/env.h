#ifndef _UTIL_ENV_H
#define _UTIL_ENV_H
#include <stdbool.h>

/* Get integer, string, or comma delimited array of ints from the environment
 * by name, or if not set, return (copy in string/int* case) of default arg.
 */
int env_getint (char *name, int dflt);
char *env_getstr (char *name, char *dflt);
int env_getints (char *name, int **iap, int *lenp, int dflt_ia[], int dflt_len);
bool env_getbool (char *name, bool dflt);

#endif /* !_UTIL_ENV_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
