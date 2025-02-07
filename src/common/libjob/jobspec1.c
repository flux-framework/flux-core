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
#include <jansson.h>
#include <string.h>
#include <stdbool.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "jobspec1.h"
#include "jobspec1_private.h"

struct flux_jobspec1 {
    json_t *obj;
};

/* Return a new reference to a json array of strings. */
static json_t *argv_to_json (int argc, char **argv)
{
    int i;
    json_t *a, *o;

    if (!(a = json_array ()))
        goto nomem;
    for (i = 0; i < argc; i++) {
        if (!(o = json_string (argv[i])) || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto nomem;
        }
    }
    return a;
nomem:
    json_decref (a);
    errno = ENOMEM;
    return NULL;
}

static json_t *jobspec1_attr_get (flux_jobspec1_t *jobspec, const char *name)
{
    char *path;
    json_t *val;

    if (!jobspec || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (asprintf (&path, "attributes.%s", name) < 0)
        return NULL;
    val = jpath_get (jobspec->obj, path);
    ERRNO_SAFE_WRAP (free, path);
    return val;
}

static int jobspec1_attr_set (flux_jobspec1_t *jobspec,
                              const char *name,
                              json_t *val)
{
    char *path;
    int rc;

    if (!jobspec || !name || !val) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&path, "attributes.%s", name) < 0)
        return -1;
    rc = jpath_set (jobspec->obj, path, val);
    ERRNO_SAFE_WRAP (free, path);
    return rc;
}

int flux_jobspec1_attr_del (flux_jobspec1_t *jobspec, const char *name)
{
    char *path;
    int rc;

    if (!jobspec || !name) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&path, "attributes.%s", name) < 0)
        return -1;
    rc = jpath_del (jobspec->obj, path);
    ERRNO_SAFE_WRAP (free, path);
    return rc;
}

int flux_jobspec1_attr_unpack (flux_jobspec1_t *jobspec,
                               const char *path,
                               const char *fmt,
                               ...)
{
    va_list ap;
    json_t *val;
    int rc;

    if (!fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!(val = jobspec1_attr_get (jobspec, path)))
        return -1;
    va_start (ap, fmt);
    rc = json_vunpack_ex (val, NULL, 0, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_jobspec1_attr_pack (flux_jobspec1_t *jobspec,
                             const char *path,
                             const char *fmt,
                             ...)
{
    va_list ap;
    json_t *val;

    if (!jobspec || !path || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    val = json_vpack_ex (NULL, 0, fmt, ap);
    va_end (ap);
    if (!val) {
        errno = EINVAL;
        return -1;
    }
    if (jobspec1_attr_set (jobspec, path, val) < 0) {
        ERRNO_SAFE_WRAP (json_decref, val);
        return -1;
    }
    json_decref (val);
    return 0;
}

static int tasks_check (json_t *tasks, flux_jobspec1_error_t *error)
{
    json_error_t json_error;
    json_t *argv_json;
    const char *slot;
    json_t *count;
    size_t index;
    json_t *value;
    int n;

    if (json_unpack_ex (tasks,
                        &json_error,
                        0,
                        "[{s:o s:s s:o !}]",
                        "command", &argv_json,
                        "slot", &slot,
                        "count", &count) < 0) {
        errprintf (error, "tasks section: %s", json_error.text);
        goto error;
    }
    if (!json_is_array (argv_json)) {
        errprintf (error, "tasks command must be an array");
        goto error;
    }
    if (json_array_size (argv_json) < 1) {
        errprintf (error, "tasks command array length must be >= 1");
        goto error;
    }
    json_array_foreach (argv_json, index, value) {
        if (!json_is_string (value)) {
            errprintf (error, "tasks command array entry must be a string");
            goto error;
        }
    }
    if (json_object_size (count) != 1) {
        errprintf (error, "tasks count must have exactly one key set");
        goto error;
    }
    else if (json_unpack (count, "{s:i}", "per_slot", &n) == 0) {
        if (n < 1) {
            errprintf (error, "tasks per_slot count must be >= 1");
            goto error;
        }
    }
    else if (json_unpack (count, "{s:i}", "total", &n) == 0) {
        if (n < 1) {
            errprintf (error, "tasks total count must be >= 1");
            goto error;
        }
    }
    else {
        errprintf (error, "tasks count object is malformed");
        goto error;
    }
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int slot_vertex_check (json_t *slot, flux_jobspec1_error_t *error)
{
    json_error_t json_error;
    const char *type;
    int count;
    json_t *with;
    const char *label;
    int exclusive = 0;
    size_t index;
    json_t *value;

    if (json_unpack_ex (slot,
                        &json_error,
                        0,
                        "{s:s s:i s:o s:s s?b !}",
                        "type", &type,
                        "count", &count,
                        "with", &with,
                        "label", &label,
                        "exclusive", &exclusive) < 0) {
        errprintf (error, "slot vertex: %s", json_error.text);
        goto error;
    }
    if (count < 1) {
        errprintf (error, "slot count must be >= 1");
        goto error;
    }
    if (!json_is_array (with)) {
        errprintf (error, "slot with must be an array");
        goto error;
    }
    if (json_array_size (with) != 1 && json_array_size (with) != 2) {
        errprintf (error, "slot with array must have 1-2 elements");
        goto error;
    }
    json_array_foreach (with, index, value) {
        int min_count = 0;
        if (json_unpack_ex (value,
                            &json_error,
                            0,
                            "{s:s s:i !}",
                            "type", &type,
                            "count", &count) < 0) {
            errprintf (error, "slot with: %s", json_error.text);
            goto error;
        }
        if (!streq (type, "core") && !streq (type, "gpu")) {
            errprintf (error, "slot with type must be core or gpu");
            goto error;
        }
        if (streq (type, "core"))
            min_count = 1;
        if (count < min_count) {
            errprintf (error, "slot %s count must be >= %d", type, min_count);
            goto error;
        }
    }
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int node_vertex_check (json_t *node, flux_jobspec1_error_t *error)
{
    json_error_t json_error;
    const char *type;
    int count;
    json_t *with;
    const char *unit = NULL;
    size_t index;
    json_t *value;

    if (json_unpack_ex (node,
                        &json_error,
                        0,
                        "{s:s s:i s:o s?s !}",
                        "type", &type,
                        "count", &count,
                        "with", &with,
                        "unit", &unit) < 0) {
        errprintf (error, "node vertex: %s", json_error.text);
        goto error;
    }
    if (count < 1) {
        errprintf (error, "node count must be >= 1");
        goto error;
    }
    if (!json_is_array (with)) {
        errprintf (error, "node with must be an array");
        goto error;
    }
    if (json_array_size (with) != 1) {
        errprintf (error, "node with array must have 1 elements");
        goto error;
    }
    json_array_foreach (with, index, value) {
        if (slot_vertex_check (value, error) < 0)
            goto error;
    }
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int resources_check (json_t *res, flux_jobspec1_error_t *error)
{
    json_error_t json_error;
    json_t *vertex;
    const char *type;

    if (json_unpack_ex (res, &json_error, 0, "[o]", &vertex) < 0) {
        errprintf (error, "resources section: %s", json_error.text);
        goto error;
    }
    if (json_unpack_ex (vertex, &json_error, 0, "{s:s}", "type", &type) < 0) {
        errprintf (error, "resource vertex: %s", json_error.text);
        goto error;
    }
    if (streq (type, "node")) {
        if (node_vertex_check (vertex, error) < 0)
            goto error;
    }
    else if (streq (type, "slot")) {
        if (slot_vertex_check (vertex, error) < 0)
            goto error;
    }
    else {
        errprintf (error, "unknown resource vertex type");
        goto error;
    }
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int attr_system_check (json_t *o, flux_jobspec1_error_t *error)
{
    const char *key;
    json_t *value;
    bool has_duration = false;

    json_object_foreach (o, key, value) {
        if (streq (key, "duration")) {
            if (!json_is_number (value)) {
                errprintf (error,
                           "attributes.system.duration must be a number");
                return -1;
            }
            has_duration = true;
        }
        else if (streq (key, "environment")) {
            if (!(json_is_object (value))) {
                errprintf (error,
                         "attributes.system.environment must be a dictionary");
                return -1;
            }
        }
        else if (streq (key, "constraints")) {
            if (!(json_is_object (value))) {
                errprintf (error,
                         "attributes.system.constraints must be a dictionary");
                return -1;
            }
        }
        else if (streq (key, "dependencies")) {
            size_t index;
            json_t *el;
            const char *scheme;
            const char *val;

            if (!json_is_array (value)) {
                errprintf (error,
                           "attributes.system.dependencies must be an array");
                return -1;
            }
            json_array_foreach (value, index, el) {
                if (!json_is_object (el)) {
                    errprintf (error,
                               "attributes.system.dependencies elements"
                               " must be an object");
                    return -1;
                }
                if (json_unpack (el,
                                 "{s:s s:s}",
                                 "scheme", &scheme,
                                 "value", &val) < 0) {
                    errprintf (error,
                               "attributes.system.dependencies elements"
                               " must contain scheme and value strings");
                    return -1;
                }
            }
        }
        else if (streq (key, "shell")) {
            json_t *opt;
            if ((opt = json_object_get (value, "options"))
                && !json_is_object (opt)) {
                errprintf (error,
                           "attributes.shell.options must be a dictionary");
                return -1;
            }
        }
    }
    if (!has_duration) {
        errprintf (error, "attributes.system.duration is required");
        return -1;
    }
    return 0;
}

int flux_jobspec1_attr_check (flux_jobspec1_t *jobspec,
                              flux_jobspec1_error_t *error)
{
    json_t *o;
    const char *key;
    json_t *value;
    bool has_system = false;

    if (!jobspec) {
        errprintf (error, "jobspec must not be NULL");
        goto error;
    }
    if (!(o = json_object_get (jobspec->obj, "attributes"))) {
        errprintf (error, "attributes must exist");
        goto error;
    }
    if (!json_is_object (o)) {
        errprintf (error, "attributes must be an object");
        goto error;
    }
    json_object_foreach (o, key, value) {
        if (streq (key, "user")) {
            if (json_object_size (value) == 0) {
                errprintf (error,
                           "if present, attributes.user must contain values");
                goto error;
            }
        }
        else if (streq (key, "system")) {
            if (json_object_size (value) == 0) {
                errprintf (error,
                           "if present, attributes.system must contain values");
                goto error;
            }
            if (attr_system_check (value, error) < 0)
                goto error;
            has_system = true;
        }
        else {
            errprintf (error, "unknown attributes section %s", key);
            goto error;
        }
    }
    if (!has_system) {
        errprintf (error, "attributes.system is required");
        goto error;
    }
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_jobspec1_check (flux_jobspec1_t *jobspec, flux_jobspec1_error_t *error)
{
    json_error_t json_error;
    json_t *resources;
    json_t *tasks;
    json_t *attributes;
    int version;

    if (!jobspec) {
        errprintf (error, "jobspec cannot be NULL");
        goto error;
    }
    if (json_unpack_ex (jobspec->obj,
                        &json_error,
                        0,
                        "{s:o s:o s:o s:i !}",
                        "resources", &resources,
                        "tasks", &tasks,
                        "attributes", &attributes,
                        "version", &version) < 0) {
        errprintf (error, "jobspec object: %s", json_error.text);
        goto error;
    }
    if (version != 1) {
        errprintf (error, "only version 1 jobspec is supported");
        goto error;
    }
    if (resources_check (resources, error) < 0)
        goto error;
    if (tasks_check (tasks, error) < 0)
        goto error;
    if (flux_jobspec1_attr_check (jobspec, error) < 0)
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_jobspec1_unsetenv (flux_jobspec1_t *jobspec, const char *name)
{
    char *path;

    if (!jobspec || !name) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&path, "system.environment.%s", name) < 0)
        return -1;
    if (flux_jobspec1_attr_del (jobspec, path) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        return -1;
    }
    return 0;
}

int flux_jobspec1_setenv (flux_jobspec1_t *jobspec,
                           const char *name,
                           const char *value,
                           int overwrite)
{
    char *path;
    const char *s;

    if (!jobspec || !name || !value) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&path, "system.environment.%s", name) < 0)
        return -1;
    if (!overwrite) {
        if (flux_jobspec1_attr_unpack (jobspec, path, "s", &s) == 0) {
            free (path);
            return 0;
        }
    }
    if (flux_jobspec1_attr_pack (jobspec, path, "s", value) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        return -1;
    }
    free (path);
    return 0;
}

static int jobspec1_putenv (flux_jobspec1_t *jobspec, const char *entry)
{
    char *name;
    char *value;
    int rc = -1;

    if (!(name = strdup (entry)))
        return -1;
    if ((value = strchr (name, '=')))
        *value++ = '\0';
    if (!value || name[0] == '\0') {
        errno = EINVAL;
        goto done;
    }
    rc = flux_jobspec1_setenv (jobspec, name, value, 1);
done:
    ERRNO_SAFE_WRAP (free, name);
    return rc;
}

/* 'stdio_name' should be one of: 'output.stdout', 'output.stderr',
 * or 'input.stdin'.
 */
static int flux_jobspec1_set_stdio (flux_jobspec1_t *jobspec,
                                    const char *stdio_name,
                                    const char *path)
{
    char key[256];

    if (!jobspec || !path || (!streq (stdio_name, "input.stdin")
        && !streq (stdio_name, "output.stdout")
        && !streq (stdio_name, "output.stderr"))) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf (key, sizeof (key), "system.shell.options.%s", stdio_name)
        >= sizeof (key)) {
        errno = EOVERFLOW;
        return -1;
    }
    return flux_jobspec1_attr_pack (jobspec,
                                    key,
                                    "{s:s s:s}",
                                    "type",
                                    "file",
                                    "path",
                                    path);
}

int flux_jobspec1_set_stdin (flux_jobspec1_t *jobspec, const char *path)
{
    return flux_jobspec1_set_stdio (jobspec, "input.stdin", path);
}

int flux_jobspec1_set_stdout (flux_jobspec1_t *jobspec, const char *path)
{
    return flux_jobspec1_set_stdio (jobspec, "output.stdout", path);
}

int flux_jobspec1_set_stderr (flux_jobspec1_t *jobspec, const char *path)
{
    return flux_jobspec1_set_stdio (jobspec, "output.stderr", path);
}

int flux_jobspec1_set_cwd (flux_jobspec1_t *jobspec, const char *cwd)
{
    if (!cwd) {  // 'jobspec' checked by 'attr_pack'
        errno = EINVAL;
        return -1;
    }
    return flux_jobspec1_attr_pack (jobspec, "system.cwd", "s", cwd);
}

char *flux_jobspec1_encode (flux_jobspec1_t *jobspec, size_t flags)
{
    char *returnval;

    if (!jobspec) {
        errno = EINVAL;
        return NULL;
    }
    if (!(returnval = json_dumps (jobspec->obj, flags))) {
        errno = ENOMEM;
        return NULL;
    }
    return returnval;
}

flux_jobspec1_t *jobspec1_from_json (json_t *obj)
{
    flux_jobspec1_t *jobspec;

    if (!obj) {
        errno = EINVAL;
        return NULL;
    }
    if (!(jobspec = calloc (1, sizeof (*jobspec))))
        return NULL;
    jobspec->obj = json_incref (obj);
    return jobspec;
}

flux_jobspec1_t *flux_jobspec1_decode (const char *s,
                                       flux_jobspec1_error_t *error)
{
    flux_jobspec1_t *jobspec;
    json_t *obj;
    json_error_t json_error;

    if (!s) {
        errno = EINVAL;
        errprintf (error, "%s", strerror (errno));
        return NULL;
    }
    if (!(obj = json_loads (s, 0, &json_error))) {
        errno = EINVAL;
        errprintf (error, "%s", json_error.text);
        return NULL;
    }
    if (!(jobspec = jobspec1_from_json (obj))) {
        ERRNO_SAFE_WRAP (json_decref, obj);
        errprintf (error, "%s", strerror (errno));
        return NULL;
    }
    json_decref (obj);
    return jobspec;
}

static json_t *tasks_create (int argc, char **argv)
{
    json_t *tasks;
    json_t *argv_json;

    if (!(argv_json = argv_to_json (argc, argv))) {
        return NULL;
    }
    if (!(tasks = json_pack ("[{s:o s:s s:{s:i}}]",
                             "command",
                             argv_json,
                             "slot",
                             "task",
                             "count",
                             "per_slot",
                             1))) {
        json_decref (argv_json);
        errno = ENOMEM;
        return NULL;
    }
    return tasks;
}

/* Create and return the 'resources' section of a jobspec.
 * Return a new reference on success, NULL with errno set on error.
 * Negative values of 'ntasks' and 'cores_per_task' are interpreted as 1.
 * Negative values for 'gpus_per_task' and 'nnodes' are ignored.
 */
static json_t *resources_create (int ntasks,
                                 int cores_per_task,
                                 int gpus_per_task,
                                 int nnodes)
{
    json_t *slot;

    if (cores_per_task < 1)
        cores_per_task = 1;
    if (ntasks < 1)
        ntasks = 1;
    if (nnodes > ntasks) {
        errno = EINVAL;
        return NULL;
    }
    if (gpus_per_task > 0) {
        if (!(slot = json_pack ("[{s:s s:i s:[{s:s s:i} {s:s s:i}] s:s}]",
                                "type", "slot",
                                "count", ntasks,
                                "with",
                                "type", "core",
                                "count", cores_per_task,
                                "type", "gpu",
                                "count", gpus_per_task,
                                "label", "task")))
            goto nomem;
    }
    else {
        if (!(slot = json_pack ("[{s:s s:i s:[{s:s s:i}] s:s}]",
                                "type", "slot",
                                "count", ntasks,
                                "with",
                                "type", "core",
                                "count", cores_per_task,
                                "label", "task")))
            goto nomem;
    }
    if (nnodes > 0) {
        json_t *node;
        if (!(node = json_pack ("[{s:s s:i s:o}]",
                                "type", "node",
                                "count", nnodes,
                                "with",
                                slot))) {
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

flux_jobspec1_t *flux_jobspec1_from_command (int argc,
                                             char **argv,
                                             char **env,
                                             int ntasks,
                                             int cores_per_task,
                                             int gpus_per_task,
                                             int nnodes,
                                             double duration)
{
    json_t *resources;
    json_t *tasks;
    json_t *obj;
    flux_jobspec1_t *jobspec;

    // resource arguments are checked by 'resources_create'
    if (argc < 0 || !argv || duration < 0.0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(tasks = tasks_create (argc, argv))
        || !(resources = resources_create (ntasks,
                                           cores_per_task,
                                           gpus_per_task,
                                           nnodes))) {
        ERRNO_SAFE_WRAP (json_decref, tasks);
        return NULL;
    }
    if (!(obj = json_pack ("{s:o, s:o, s:{s:{s:f, s:{}}}, s:i}",
                           "resources", resources,
                           "tasks", tasks,
                           "attributes",
                             "system",
                               "duration", duration,
                             "environment",
                           "version", 1))) {
        json_decref (tasks);
        json_decref (resources);
        errno = ENOMEM;
        return NULL;
    }
    if (!(jobspec = jobspec1_from_json (obj))) {
        ERRNO_SAFE_WRAP (json_decref, obj);
        return NULL;
    }
    json_decref (obj);

    if (env) {
        int i;
        for (i = 0; env[i] != NULL; i++) {
            if (jobspec1_putenv (jobspec, env[i]) < 0)
                goto error;
        }
    }
    return jobspec;

error:
    flux_jobspec1_destroy (jobspec);
    return NULL;
}

void flux_jobspec1_destroy (flux_jobspec1_t *jobspec)
{
    int saved_errno = errno;

    if (jobspec) {
        json_decref (jobspec->obj);
        free (jobspec);
        errno = saved_errno;
    }
}

json_t *jobspec1_get_json (flux_jobspec1_t *jobspec)
{
    return jobspec->obj;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
