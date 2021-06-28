/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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

#include "src/common/libutil/errno_safe.h"

#include "jpath.h"

static int jpath_set_destructive (json_t *o, char *path, json_t *val)
{
    char *cp;
    json_t *dir;

    if ((cp = strchr (path, '.'))) {
        *cp++ = '\0';
        if (strlen (path) == 0) {
            errno = EINVAL;
            return -1;
        }
        if (!(dir = json_object_get (o, path))) {
            if (!(dir = json_object ()))
                goto nomem;
            if (json_object_set_new (o, path, dir) < 0) {
                json_decref (dir);
                goto nomem;
            }
        }
        return jpath_set_destructive (dir, cp, val);
    }

    if (strlen (path) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (json_object_set (o, path, val) < 0)
        goto nomem;
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

static int jpath_del_destructive (json_t *o, char *path)
{
    char *cp;
    json_t *dir;

    if ((cp = strchr (path, '.'))) {
        *cp++ = '\0';
        if (strlen (path) == 0) {
            errno = EINVAL;
            return -1;
        }
        if (!(dir = json_object_get (o, path)))
            return 0;
        return jpath_del_destructive (dir, cp);
    }

    if (strlen (path) == 0) {
        errno = EINVAL;
        return -1;
    }
    (void)json_object_del (o, path);
    return 0;
}

static json_t *jpath_get_destructive (json_t *o, char *path)
{
    char *cp;
    json_t *dir;
    json_t *val;

    if ((cp = strchr (path, '.'))) {
        *cp++ = '\0';
        if (strlen (path) == 0) {
            errno = EINVAL;
            return NULL;
        }
        if (!(dir = json_object_get (o, path))) {
            errno = ENOENT;
            return NULL;
        }
        return jpath_get_destructive (dir, cp);
    }

    if (strlen (path) == 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(val = json_object_get (o, path))) {
        errno = ENOENT;
        return NULL;
    }
    return val;
}

int jpath_set (json_t *o, const char *path, json_t *val)
{
    char *cpy;
    int rc;

    if (!o || !path || !val) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (path)))
        return -1;
    rc = jpath_set_destructive (o, cpy, val);
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;
}

int jpath_del (json_t *o, const char *path)
{
    char *cpy;
    int rc;

    if (!o || !path) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (path)))
        return -1;
    rc = jpath_del_destructive (o, cpy);
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;
}

json_t *jpath_get (json_t *o, const char *path)
{
    char *cpy;
    json_t *ret;

    if (!o || !path) {
        errno = EINVAL;
        return NULL;
    }
    if (!(cpy = strdup (path)))
        return NULL;
    ret = jpath_get_destructive (o, cpy);
    ERRNO_SAFE_WRAP (free, cpy);
    return ret;
}

// vi:ts=4 sw=4 expandtab
