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

struct builtin {
    char *key;
    const char *val_installed;
    const char *val_intree;
};

static struct builtin builtin_tab[] = {
    { "lua_cpath_add",  INSTALLED_LUA_CPATH_ADD,    INTREE_LUA_CPATH_ADD },
    { "lua_path_add",   INSTALLED_LUA_PATH_ADD,     INTREE_LUA_PATH_ADD },
    { "python_path",    INSTALLED_PYTHON_PATH,      INTREE_PYTHON_PATH },
    { "man_path",       INSTALLED_MAN_PATH,         INTREE_MAN_PATH },
    { "exec_path",      INSTALLED_EXEC_PATH,        INTREE_EXEC_PATH },
    { "connector_path", INSTALLED_CONNECTOR_PATH,   INTREE_CONNECTOR_PATH },
    { "module_path",    INSTALLED_MODULE_PATH,      INTREE_MODULE_PATH },
    { "rc1_path",       INSTALLED_RC1_PATH,         INTREE_RC1_PATH },
    { "rc3_path",       INSTALLED_RC3_PATH,         INTREE_RC3_PATH },
    { "cf_path",        INSTALLED_CF_PATH,          INTREE_CF_PATH },
    { "cmdhelp_pattern",INSTALLED_CMDHELP_PATTERN,  INTREE_CMDHELP_PATTERN },
    { "pmi_library_path",
                        INSTALLED_PMI_LIBRARY_PATH, INTREE_PMI_LIBRARY_PATH },
    { "shell_path",     INSTALLED_SHELL_PATH,       INTREE_SHELL_PATH },
    { "shell_pluginpath",
                        INSTALLED_SHELL_PLUGINPATH, INTREE_SHELL_PLUGINPATH },
    { "shell_initrc",   INSTALLED_SHELL_INITRC,     INTREE_SHELL_INITRC },
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

const char *flux_conf_builtin_get (const char *name,
                                   enum flux_conf_flags flags)
{
    struct builtin *entry;
    bool intree = false;
    const char *val = NULL;

    switch (flags) {
        case FLUX_CONF_INSTALLED:
            break;
        case FLUX_CONF_INTREE:
            intree = true;
            break;
    }
    for (entry = &builtin_tab[0]; entry->key != NULL; entry++) {
        if (!strcmp (entry->key, name)) {
            val = intree ? entry->val_intree : entry->val_installed;
            break;
        }
    }
    if (!val)
        goto error;
    return val;
error:
    errno = EINVAL;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
