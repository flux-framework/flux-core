/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _STRV_H
#define _STRV_H

/* string vector convenience functions */

/* Create a string vector from the given string, using the characters
 * in 'delim' as separators.  str can be NULL to return an array with
 * only the NULL delimiter string set.
 */
char **strv_create (char *str, char *delim);

/* Destroy a string vector */
void strv_destroy (char **strv);

/* Copy a string vector. strv can be NULL to return an array with only
 * the NULL delimiter string set */
int strv_copy (char **strv, char ***strv_cpy);

#endif /* !_STRV_H */
