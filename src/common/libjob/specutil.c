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
#include <string.h>
#include <jansson.h>
#include <errno.h>

#include "src/common/libutil/errno_safe.h"

#include "specutil.h"

json_t *specutil_argv_create (int argc, char **argv)
{
    int i;
    json_t *a, *o;

    if (!(a = json_array ()))
        goto nomem;
    for (i = 0; i < argc; i++) {
        if (!(o = json_string (argv[i])) || json_array_append_new (a, o) < 0) {
            ERRNO_SAFE_WRAP (json_decref, o);
            goto nomem;
        }
    }
    return a;
nomem:
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

int specutil_env_set (json_t *o, const char *name, const char *value)
{
    json_t *val;

    if (!(val = json_string (value)))
        goto nomem;
    if (json_object_set_new (o, name, val) < 0)
        goto nomem;
    return 0;
nomem:
    json_decref (val);
    errno = ENOMEM;
    return -1;
}

int specutil_env_unset (json_t *o, const char *name)
{
    (void)json_object_del (o, name);
    return 0;
}

int specutil_env_put (json_t *o, const char *entry)
{
    char *cpy;
    char *cp;
    int rc = -1;

    if (!(cpy = strdup (entry)))
        return -1;
    if ((cp = strchr (cpy, '=')))
        *cp++ = '\0';
    if (!cp || cp == cpy) {
        errno = EINVAL;
        goto done;
    }
    rc = specutil_env_set (o, cpy, cp);
done:
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;
}

json_t *specutil_env_create (char **env)
{
    int i;
    json_t *o;

    if (!(o = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    for (i = 0; env[i] != NULL; i++) {
        if (specutil_env_put (o, env[i]) < 0)
            goto error;
    }
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

/* Recursively set path=val in object 'o'.
 * A period '.' is interpreted as a path separator.
 * Path components are created as needed.
 * N.B. 'path' is modified.
 */
static int object_set_path (json_t *o, char *path, json_t *val)
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
        return object_set_path (dir, cp, val);
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

/* Recursively delete path in object 'o'.
 * A period '.' is interpreted as a path separator.
 * If the target or path leading to it does not exist, return succces.
 * N.B. 'path' is modified.
 */
int object_del_path (json_t *o, char *path)
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
        return object_del_path (dir, cp);
    }

    if (strlen (path) == 0) {
        errno = EINVAL;
        return -1;
    }
    (void)json_object_del (o, path);
    return 0;
}

static json_t *object_get_path (json_t *o, char *path)
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
        return object_get_path (dir, cp);
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

int specutil_attr_del (json_t *o, const char *path)
{
    char *cpy;
    int rc;

    if (!o || !path) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (path)))
        return -1;
    rc = object_del_path (o, cpy);
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;
}

int specutil_attr_set (json_t *o, const char *path, json_t *val)
{
    char *cpy;
    int rc;

    if (!o || !path || !val) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (path)))
        return -1;
    rc = object_set_path (o, cpy, val);
    ERRNO_SAFE_WRAP (free, cpy);
    return rc;
}

json_t *specutil_attr_get (json_t *o, const char *path)
{
    char *cpy;
    json_t *val;

    if (!o || !path) {
        errno = EINVAL;
        return NULL;
    }
    if (!(cpy = strdup (path)))
        return NULL;
    val = object_get_path (o, cpy);
    ERRNO_SAFE_WRAP (free, cpy);
    return val;
}


static int specutil_attr_vpack (json_t *o,
                                const char *path,
                                const char *fmt,
                                va_list ap)
{
    json_t *value;
    int rc;

    if (!fmt || !(value = json_vpack_ex (NULL, 0, fmt, ap))) {
        errno = EINVAL;
        return -1;
    }
    rc = specutil_attr_set (o, path, value);
    ERRNO_SAFE_WRAP (json_decref, value);
    return rc;
}

int specutil_attr_pack (json_t *o, const char *path, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start (ap, fmt);
    rc = specutil_attr_vpack (o, path, fmt, ap);
    va_end (ap);
    return rc;
}

static int specutil_attr_system_check (json_t *o, const char **errtxt)
{
    const char *key;
    json_t *value;

    json_object_foreach (o, key, value) {
        if (!strcmp (key, "duration")) {
            if (!json_is_number (value)) {
                *errtxt = "attributes.system.duration must be a number";
                return -1;
            }
        }
        else if (!strcmp (key, "environment")) {
            if (!(json_is_object (value))) {
                *errtxt = "attributes.system.environment.must be a dictionary";
                return -1;
            }
        }
        else if (!strcmp (key, "shell")) {
            json_t *opt;
            if ((opt = json_object_get (value, "options"))
                && !json_is_object (opt)) {
                *errtxt = "attributes.shell.options must be a dictionary";
                return -1;
            }
        }
    }
    return 0;
}

int specutil_attr_check (json_t *o, char *errbuf, int errbufsz)
{
    const char *key;
    json_t *value;
    const char *errtxt;

    json_object_foreach (o, key, value) {
        if (!strcmp (key, "user")) {
            if (json_object_size (value) == 0) {
                errtxt = "if present, attributes.user must contain values";
                goto copy_error;
            }
        }
        else if (!strcmp (key, "system")) {
            if (json_object_size (value) == 0) {
                errtxt = "if present, attributes.system must contain values";
                goto copy_error;
            }
            if (specutil_attr_system_check (value, &errtxt) < 0)
                goto copy_error;
        }
        else {
            snprintf (errbuf, errbufsz, "unknown attributes section %s", key);
            goto error;
        }
    }
    return 0;
copy_error:
    snprintf (errbuf, errbufsz, "%s", errtxt);
error:
    errno = EINVAL;
    return -1;
}

static json_t *specutil_tasks_create (json_t *argv)
{
    json_t *tasks;

    if (!(tasks = json_pack ("[{s:O s:s s:{s:i}}]",
                             "command", argv,
                             "slot", "task",
                             "count",
                               "per_slot", 1))) {
        errno = ENOMEM;
        return NULL;
    }
    return tasks;
}

static json_t *specutil_resources_create (struct resource_param *param)
{
    json_t *slot;
    struct resource_param p = *param;

    if (p.cores_per_task < 1)
        p.cores_per_task = 1;
    if (p.ntasks < 1)
        p.ntasks = 1;
    if (p.nodes > p.ntasks) {
        errno = EINVAL;
        return NULL;
    }
    if (p.gpus_per_task > 0) {
        if (!(slot = json_pack ("[{s:s s:i s:[{s:s s:i} {s:s s:i}] s:s}]",
                                "type", "slot",
                                "count", p.ntasks,
                                "with",
                                  "type", "core",
                                  "count", p.cores_per_task,
                                  "type", "gpu",
                                  "count", p.gpus_per_task,
                                "label", "task")))
            goto nomem;
    }
    else {
        if (!(slot = json_pack ("[{s:s s:i s:[{s:s s:i}] s:s}]",
                                "type", "slot",
                                "count", p.ntasks,
                                "with",
                                "type", "core",
                                "count", p.cores_per_task,
                                "label", "task")))
            goto nomem;
    }
    if (p.nodes > 0) {
        json_t *node;
        if (!(node = json_pack ("[{s:s s:i s:o}]",
                                "type", "node",
                                "count", p.nodes,
                                "with", slot))) {
            json_decref (slot);
            goto nomem;
        }
        return node;
    }
    return slot;
nomem:
    errno = ENOMEM;
    return NULL;
}

json_t *specutil_jobspec_create (json_t *attributes,
                                 json_t *argv,
                                 struct resource_param *param,
                                 char *errbuf,
                                 int errbufsz)
{
    json_t *tasks = NULL;
    json_t *resources = NULL;
    json_t *jobspec;

    if (specutil_attr_check (attributes, errbuf, errbufsz) < 0)
        goto error;
    if (!(tasks = specutil_tasks_create (argv))) {
        snprintf (errbuf, errbufsz, "Error creating tasks object");
        goto error;
    }
    if (!(resources = specutil_resources_create (param))) {
        snprintf (errbuf, errbufsz, "Error creating resources object");
        goto error;
    }
    if (!(jobspec = json_pack ("{s:o s:o s:O s:i}",
                               "resources", resources,
                               "tasks", tasks,
                               "attributes", attributes, // incref
                               "version", 1))) {
        errno = ENOMEM;
        snprintf (errbuf, errbufsz, "Error creating jobspec object");
        goto error;
    }
    return jobspec;
error:
    ERRNO_SAFE_WRAP (json_decref, resources);
    ERRNO_SAFE_WRAP (json_decref, tasks);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
