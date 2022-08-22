/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "util.h"

char *util_join_arguments (json_t *o)
{
    int n = 0;
    size_t index;
    json_t *value;
    const char *arg;
    char *result;

    if (!json_is_array (o)) {
        errno = EINVAL;
        return NULL;
    }

    json_array_foreach (o, index, value) {
        if (index > 0)
            n++; // space for comma
        if (!(arg = json_string_value (value))) {
            errno = EINVAL;
            return NULL;
        }
        n += strlen (arg);
    }
    n++; // space for terminating \0

    if (!(result = calloc (1, n)))
        return NULL;

    json_array_foreach (o, index, value) {
        if (index > 0)
            strcat (result, ",");
        strcat (result, json_string_value (value));
    }

    return result;
}

// vi:tabstop=4 shiftwidth=4 expandtab
