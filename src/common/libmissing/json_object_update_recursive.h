/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBMISSING_JANNSON_UPDATE_RECURSIVE_H
#define LIBMISSING_JANNSON_UPDATE_RECURSIVE_H 1

/* Like json_object_update(), but object values in *other* are
 * recursively merged the corresponding values in *object* if they
 * are also objects, instead of overwriting them. Returns a 0 on
 * successs or -1 on error.
 *
 * Note: This version doesn't detect cycles like the version found
 * in jansson >= 2.13.1.
 */
int json_object_update_recursive (json_t *object, json_t *other);

#endif
