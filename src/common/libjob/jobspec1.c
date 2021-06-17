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
    va_start (ap, fmt);
    if (!(root = specutil_attr_get (json_object_get (jobspec->obj,
                                                     "attributes"),
                                    path))) {
        return -1;
    }
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
                              char *errbuf,
                              int errbufsz)
{
    if (!jobspec || !errbuf || errbufsz < 0) {
        errno = EINVAL;
        return -1;
    }
    return specutil_attr_check (json_object_get (jobspec->obj, "attributes"),
                                errbuf,
                                errbufsz);
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
    if (!(jobspec = malloc (sizeof (flux_jobspec1_t)))) {
        ERRNO_SAFE_WRAP (json_decref, obj);
        return NULL;
    }
    jobspec->obj = obj;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
