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
#include <errno.h>

#include "src/common/libutil/intree.h"
#include "ccan/str/str.h"

#include "conf_builtin.h"

struct builtin {
    char *key;
    const char *val_installed;
    const char *val_intree;
};

static struct builtin builtin_tab[] = {
    {
        .key = "confdir",
        .val_installed = FLUXCONFDIR,
        .val_intree = ABS_TOP_SRCDIR "/etc",
    },
    {
        .key = "libexecdir",
        .val_installed = FLUXLIBEXECDIR,
        .val_intree = ABS_TOP_SRCDIR "/etc",
    },
    {
        .key = "datadir",
        .val_installed = FLUXDATADIR,
        .val_intree = ABS_TOP_SRCDIR "/etc",
    },
    {
        .key = "lua_cpath_add",
        .val_installed = LUAEXECDIR "/?.so",
        .val_intree = ABS_TOP_BUILDDIR "/src/bindings/lua/?.so",
    },
    {
        .key = "lua_path_add",
        .val_installed = LUADIR "/?.lua",
        .val_intree = ABS_TOP_BUILDDIR "/t/?.lua;" \
                      ABS_TOP_SRCDIR "/src/bindings/lua/?.lua",
    },
    {
        .key = "python_path",
        .val_installed = FLUXPYLINKDIR,
        .val_intree = ABS_TOP_BUILDDIR "/src/bindings/python:" \
                      ABS_TOP_SRCDIR "/src/bindings/python",
    },
    {
        .key = "python_wrapper",
        .val_installed = FLUXCMDDIR "/py-runner.py",
        .val_intree = ABS_TOP_SRCDIR "/src/cmd/py-runner.py",
    },
    {
        .key = "man_path",
        .val_installed = X_MANDIR,
        .val_intree =  ABS_TOP_BUILDDIR "/doc",
    },
    {
        .key = "exec_path",
        .val_installed = FLUXCMDDIR,
        .val_intree = ABS_TOP_BUILDDIR "/src/cmd:" \
                      ABS_TOP_SRCDIR "/src/cmd:" \
                      ABS_TOP_BUILDDIR "/src/broker",
    },
    {
        .key = "connector_path",
        .val_installed = FLUXCONNECTORDIR,
        .val_intree = ABS_TOP_BUILDDIR "/src/connectors/.libs",
    },
    {
        .key = "module_path",
        .val_installed = FLUXMODDIR,
        .val_intree = ABS_TOP_BUILDDIR "/src/modules/.libs",
    },
    {
        .key = "rc1_path",
        .val_installed = "flux modprobe rc1",
        .val_intree = "flux modprobe rc1",
    },
    {
        .key = "rc3_path",
        .val_installed = "flux modprobe rc3",
        .val_intree = "flux modprobe rc3",
    },
    {
        .key = "cmdhelp_pattern",
        .val_installed = X_DATADIR "/flux/help.d/*.json",
        .val_intree = ABS_TOP_BUILDDIR "/etc/flux/help.d/*.json",
    },
    {
        .key = "pmi_library_path",
        .val_installed = FLUXLIBDIR "/libpmi.so",
        .val_intree = ABS_TOP_BUILDDIR "/src/common/flux/.libs/libpmi.so",
    },
    {
        .key = "shell_path",
        .val_installed = FLUXLIBEXECDIR "/flux-shell",
        .val_intree = ABS_TOP_BUILDDIR "/src/shell/flux-shell",
    },
    {
        .key = "shell_pluginpath",
        .val_installed = FLUXLIBDIR "/shell/plugins",
        .val_intree = ABS_TOP_BUILDDIR "/src/shell/plugins",
    },
    {
        .key = "shell_initrc",
        .val_installed = FLUXCONFDIR "/shell/initrc.lua",
        .val_intree = ABS_TOP_SRCDIR "/src/shell/initrc.lua",
    },
    {
        .key = "jobtap_pluginpath",
        .val_installed = JOBTAP_PLUGINDIR,
        .val_intree = ABS_TOP_BUILDDIR "/src/modules/job-manager/plugins/.libs",
    },
    {
        .key = "upmi_pluginpath",
        .val_installed = FLUXLIBDIR "/upmi/plugins",
        .val_intree = ABS_TOP_BUILDDIR "/src/common/libpmi/plugins/.libs",
    },
    {
        .key = "no_docs_path",
        .val_installed = X_DATADIR "/flux/.nodocs",
        .val_intree = ABS_TOP_BUILDDIR "/etc/flux/.nodocs",
    },
    {
        .key = "rundir",
        .val_installed = X_RUNSTATEDIR "/flux",
        .val_intree = NULL
    },
    { NULL, NULL, NULL },
};

const char *flux_conf_builtin_get (const char *name,
                                   enum flux_conf_builtin_hint hint)
{
    struct builtin *entry;
    bool intree = false;
    const char *val = NULL;

    switch (hint) {
        case FLUX_CONF_INSTALLED:
            break;
        case FLUX_CONF_INTREE:
            intree = true;
            break;
        case FLUX_CONF_AUTO:
            /* In this context, if executable_is_intree() returns -1 due to
             * an unlikely internal error, we return the installed value.
             */
            if (executable_is_intree () == 1)
                intree = true;
            break;
    }
    for (entry = &builtin_tab[0]; entry->key != NULL; entry++) {
        if (name && streq (entry->key, name)) {
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
