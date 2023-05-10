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
#include "ccan/str/str.h"

#include "jpath.h"

static int update_object_recursive (json_t *orig, json_t *val)
{
    const char *key;
    json_t *value;

    json_object_foreach (val, key, value) {
        json_t *orig_value = json_object_get (orig, key);

        if (json_is_object (value)) {
            if (!json_is_object (orig_value)) {
                json_t *o = json_object ();
                if (!o || json_object_set_new (orig, key, o) < 0) {
                    errno = ENOMEM;
                    json_decref (o);
                    return -1;
                }
                orig_value = o;
            }
            if (update_object_recursive (orig_value, value) < 0)
                return -1;
        }
        else if (json_object_set (orig, key, value) < 0) {
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

static int jpath_set_destructive (json_t *o,
                                  int replace,
                                  char *path,
                                  json_t *val)
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
        return jpath_set_destructive (dir, replace, cp, val);
    }

    if (strlen (path) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (replace || !(dir = json_object_get (o, path))) {
        if (json_object_set (o, path, val) < 0)
            goto nomem;
    }
    else {
        if (update_object_recursive (dir, val) < 0)
            goto nomem;
    }
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

static int jpath_do_set (json_t *o, int replace, const char *path, json_t *val)
{
    char *cpy;
    int rc;

    if (!o || !path || !val) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (path)))
        return -1;
    rc = jpath_set_destructive (o, replace, cpy, val);
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;

}

int jpath_set (json_t *o, const char *path, json_t *val)
{
    return jpath_do_set (o, 1, path, val);
}

json_t * jpath_set_new (json_t *o, const char *path, json_t *val)
{
    json_t *new = NULL;

    if (!path || !val) {
        errno = EINVAL;
        return NULL;
    }
    if (!o) {
        if (!(new = json_object ())) {
            errno = ENOMEM;
            return NULL;
        }
        o = new;
    }
    if (jpath_set (o, path, val) < 0) {
        ERRNO_SAFE_WRAP (json_decref, new);
        return NULL;
    }
    /*  Steal reference to val */
    json_decref (val);
    return o;
}

int jpath_update (json_t *o, const char *path, json_t *val)
{
    /* Special case, allow "." to update current object */
    if (streq (path, "."))
        return update_object_recursive (o, val);
    return jpath_do_set (o, 0, path, val);
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

int jpath_clear_null (json_t *o)
{
    const char *key;
    json_t *value;
    void *tmp;

    if (!o) {
        errno = EINVAL;
        return -1;
    }
    json_object_foreach_safe (o, tmp, key, value)  {
        if (json_is_object (value)) {
            if (jpath_clear_null (value) < 0)
                return -1;
            if (json_object_size (value) == 0)
                (void) json_object_del (o, key); /* ignore key-not-found */
        }
        else if (json_is_null (value))
            (void) json_object_del (o, key); /* ignore key-not-found */
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
