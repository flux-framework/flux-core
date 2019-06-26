/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <string.h>
#include "conf.h"

struct config {
    char *key;
    const char *val;
    const char *val_intree;
};

static struct config default_config[] = {
    { "lua_cpath_add",  INSTALLED_LUA_CPATH_ADD,    INTREE_LUA_CPATH_ADD },
    { "lua_path_add",   INSTALLED_LUA_PATH_ADD,     INTREE_LUA_PATH_ADD },
    { "python_path",    INSTALLED_PYTHON_PATH,      INTREE_PYTHON_PATH },
    { "man_path",       INSTALLED_MAN_PATH,         INTREE_MAN_PATH },
    { "exec_path",      INSTALLED_EXEC_PATH,        INTREE_EXEC_PATH },
    { "connector_path", INSTALLED_CONNECTOR_PATH,   INTREE_CONNECTOR_PATH },
    { "module_path",    INSTALLED_MODULE_PATH,      INTREE_MODULE_PATH },
    { "rc1_path",       INSTALLED_RC1_PATH,         INTREE_RC1_PATH },
    { "rc3_path",       INSTALLED_RC3_PATH,         INTREE_RC3_PATH },
    { "cmdhelp_pattern",INSTALLED_CMDHELP_PATTERN,  INTREE_CMDHELP_PATTERN },
    { "pmi_library_path",
                        INSTALLED_PMI_LIBRARY_PATH, INTREE_PMI_LIBRARY_PATH },
    { "shell_path",     INSTALLED_SHELL_PATH,       INTREE_SHELL_PATH },
    { "keydir",         NULL,                       INTREE_KEYDIR },
    { "no_docs_path",   INSTALLED_NO_DOCS_PATH,     INTREE_NO_DOCS_PATH },
    { "rundir",         INSTALLED_RUNDIR,           NULL },
    { "bindir",         INSTALLED_BINDIR,           INTREE_BINDIR },
    { "jobspec_validate_path", INSTALLED_JOBSPEC_VALIDATE_PATH,
                                            INTREE_JOBSPEC_VALIDATE_PATH },
    { "jobspec_schema_path", INSTALLED_JOBSPEC_SCHEMA_PATH,
                                            INTREE_JOBSPEC_SCHEMA_PATH },
    { NULL, NULL, NULL },
};

const char *flux_conf_get (const char *name, int flags)
{
    struct config *c;

    for (c = &default_config[0]; c->key != NULL; c++) {
        if (!strcmp (c->key, name))
            return (flags & CONF_FLAG_INTREE) ? c->val_intree : c->val;
    }
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
