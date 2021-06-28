/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_JPATH_H
#define _UTIL_JPATH_H

#include <jansson.h>

/* Set 'path' to 'val' in object 'o', taking a reference on 'val'.
 * Return 0 on success, -1 on failure with errno set.
 */
int jpath_set (json_t *o, const char *path, json_t *val);

/* Delete 'path' from object 'o'.
 * Return 0 on success, -1 on failure with errno set.
 */
int jpath_del (json_t *o, const char *path);

/* Get 'path' from object 'o', without taking a reference on returned value.
 * Return value on success, NULL on failure with errno set.
 */
json_t *jpath_get (json_t *o, const char *path);

#endif /* !_UTIL_JPATH_H */

// vi:ts=4 sw=4 expandtab
