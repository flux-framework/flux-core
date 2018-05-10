/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
    { "wrexecd_path",   INSTALLED_WREXECD_PATH,     INTREE_WREXECD_PATH },
    { "wreck_lua_pattern",
                        INSTALLED_WRECK_LUA_PATTERN,
                                                    INTREE_WRECK_LUA_PATTERN },
    { "keydir",         NULL,                       INTREE_KEYDIR },
    { "no_docs_path",   INSTALLED_NO_DOCS_PATH,     INTREE_NO_DOCS_PATH },
    { "rundir",         INSTALLED_RUNDIR,           NULL },
    { "bindir",         INSTALLED_BINDIR,           INTREE_BINDIR },
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
