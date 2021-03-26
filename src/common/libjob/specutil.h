/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_SPECUTIL_H
#define _JOB_SPECUTIL_H

#include <jansson.h>

struct resource_param {
    int ntasks;
    int nodes;
    int cores_per_task;
    int gpus_per_task;
};

json_t *specutil_jobspec_create (json_t *attributes,
                                 json_t *argv,
                                 struct resource_param *param,
                                 char *errbuf,
                                 int errbufsz);

json_t *specutil_argv_create (int argc, char **argv);

json_t *specutil_env_create (char **env);
int specutil_env_put (json_t *env, const char *entry);
int specutil_env_set (json_t *env, const char *name, const char *val);
int specutil_env_unset (json_t *env, const char *name);

int specutil_attr_set (json_t *attr, const char *path, json_t *val);
int specutil_attr_pack (json_t *attr, const char *path, const char *fmt, ...);
int specutil_attr_del (json_t *attr, const char *path);

json_t *specutil_attr_get (json_t *attr, const char *path);

int specutil_attr_check (json_t *attr, char *errbuf, int errbufsz);

#endif /* _JOB_SPECUTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

