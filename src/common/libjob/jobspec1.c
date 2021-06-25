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

#include "specutil.h"
#include "src/common/libutil/errno_safe.h"

#include "jobspec1.h"
#include "jobspec1_private.h"

struct flux_jobspec1 {
    json_t *obj;
};

int flux_jobspec1_attr_unpack (flux_jobspec1_t *jobspec,
                               const char *path,
                               const char *fmt,
                               ...)
{
    va_list ap;
    json_t *root;
    int rc;

    if (!jobspec || !path || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!(root = specutil_attr_get (json_object_get (jobspec->obj,
                                                     "attributes"),
                                    path))) {
        return -1;
    }
    va_start (ap, fmt);
    rc = json_vunpack_ex (root, NULL, 0, fmt, ap);
    va_end (ap);
    return rc;
}

int flux_jobspec1_attr_del (flux_jobspec1_t *jobspec, const char *path)
{
    if (!jobspec) {  // 'path' checked by specutil_attr_del
        errno = EINVAL;
        return -1;
    }
    return specutil_attr_del (json_object_get (jobspec->obj, "attributes"),
                              path);
}

int flux_jobspec1_attr_pack (flux_jobspec1_t *jobspec,
                             const char *path,
                             const char *fmt,
                             ...)
{
    va_list ap;
    int rc;

    if (!jobspec || !path || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    rc = specutil_attr_vpack (json_object_get (jobspec->obj, "attributes"),
                              path,
                              fmt,
                              ap);

    va_end (ap);
    return rc;
}

int flux_jobspec1_attr_check (flux_jobspec1_t *jobspec,
                              flux_jobspec1_error_t *error)
{
    if (!jobspec) {
        errno = EINVAL;
        return -1;
    }
    return specutil_attr_check (json_object_get (jobspec->obj, "attributes"),
                                error ? error->text : NULL,
                                error ? sizeof (error->text) : 0);
}

static void __attribute__ ((format (printf, 2, 3)))
errprintf (flux_jobspec1_error_t *error, const char *fmt, ...)
{
    va_list ap;
    if (error) {
        va_start (ap, fmt);
        vsnprintf (error->text, sizeof (error->text), fmt, ap);
        va_end (ap);
    }
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
    if (json_unpack (count, "{s:i}", "per_slot", &n) == 0) {
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
        if (json_unpack_ex (value,
                            &json_error,
                            0,
                            "{s:s s:i !}",
                            "type", &type,
                            "count", &count) < 0) {
            errprintf (error, "slot with: %s", json_error.text);
            goto error;
        }
        if (strcmp (type, "core") != 0 && strcmp (type, "gpu") != 0) {
            errprintf (error, "slot with type must be core or gpu");
            goto error;
        }
        if (count < 1) {
            errprintf (error, "slot %s count must be >= 1", type);
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
    if (!strcmp (type, "node")) {
        if (node_vertex_check (vertex, error) < 0)
            goto error;
    }
    else if (!strcmp (type, "slot")) {
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
    if (specutil_attr_check (attributes,
                             error ? error->text : NULL,
                             error ? sizeof (error->text) : 0) < 0)
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int flux_jobspec1_unsetenv (flux_jobspec1_t *jobspec, const char *name)
{
    json_t *environment;

    if (!jobspec || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(environment = specutil_attr_get (jobspec->obj,
                                           "attributes.system.environment"))) {
        return -1;
    }
    return specutil_env_unset (environment, name);
}

int flux_jobspec1_setenv (flux_jobspec1_t *jobspec,
                           const char *name,
                           const char *value,
                           int overwrite)
{
    json_t *environment;

    if (!jobspec || !name || !value) {
        errno = EINVAL;
        return -1;
    }
    if (!(environment = specutil_attr_get (jobspec->obj,
                                           "attributes.system.environment"))) {
        return -1;
    }
    return specutil_env_set (environment, name, value, overwrite);
}

/* 'stdio_name' should be one of: 'output.stdout', 'output.stderr',
 * or 'input.stdin'.
 */
static int flux_jobspec1_set_stdio (flux_jobspec1_t *jobspec,
                                    const char *stdio_name,
                                    const char *path)
{
    char key[256];

    if (!jobspec || !path || (strcmp (stdio_name, "input.stdin")
        && strcmp (stdio_name, "output.stdout")
        && strcmp (stdio_name, "output.stderr"))) {
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

flux_jobspec1_t *flux_jobspec1_from_command (int argc,
                                             char **argv,
                                             char **env,
                                             int ntasks,
                                             int cores_per_task,
                                             int gpus_per_task,
                                             int nnodes,
                                             double duration)
{
    json_t *obj;
    json_t *resources = NULL;
    json_t *tasks = NULL;
    json_t *env_obj = NULL;
    flux_jobspec1_t *jobspec;

    // resource arguments are checked by 'resources_create'
    if (argc < 0 || !argv || duration < 0.0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(tasks = specutil_tasks_create (argc, argv))) {
        return NULL;
    }
    if (!(resources = specutil_resources_create (ntasks,
                                                 cores_per_task,
                                                 gpus_per_task,
                                                 nnodes))) {
        goto error;
    }
    if (!(env_obj = specutil_env_create (env))){
        goto error;
    }
    if (!(obj = json_pack ("{s:o, s:o, s:{s:{s:f, s:o}}, s:i}",
                           "resources", resources,
                           "tasks", tasks,
                           "attributes",
                           "system",
                           "duration", duration,
                           "environment", env_obj,
                           "version", 1))) {
        errno = ENOMEM;
        goto error;
    }
    if (!(jobspec = jobspec1_from_json (obj))) {
        ERRNO_SAFE_WRAP (json_decref, obj);
        return NULL;
    }
    json_decref (obj);
    return jobspec;

error:
    ERRNO_SAFE_WRAP (json_decref, tasks);
    ERRNO_SAFE_WRAP (json_decref, resources);
    ERRNO_SAFE_WRAP (json_decref, env_obj);
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
