/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* module_dso.c - find/open/close a broker module dso */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <flux/core.h>
#include <dlfcn.h>

#include "src/common/libflux/plugin_private.h" // for plugin_deepbind()
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/basename.h"
#include "ccan/str/str.h"

#include "module_dso.h"

void module_dso_close (void *dso)
{
    int saved_errno = errno;
#ifndef __SANITIZE_ADDRESS__
    dlclose (dso);
#endif
    errno = saved_errno;
}

/* Open DSO and set mod_mainp to the module's mod_main() function.
 * If the module defines the legacy mod_name symbol, make sure it's
 * the same as 'name'.
 * The caller must dlclose() the returned DSO pointer once mod_main()
 * is no longer needed.
 */
void *module_dso_open (const char *path,
                       const char *name, // check only
                       mod_main_f *mod_mainp,
                       flux_error_t *error)
{
    void *dso;
    mod_main_f mod_main;
    const char **mod_name;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_GLOBAL | plugin_deepbind ()))) {
        errprintf (error, "%s", dlerror ());
        errno = ENOENT;
        return NULL;
    }
    if (!(mod_main = dlsym (dso, "mod_main"))) {
        errprintf (error, "module does not define mod_main()");
        errno = EINVAL;
        goto error;
    }
    if (name && (mod_name = dlsym (dso, "mod_name")) && *mod_name != NULL) {
        if (!streq (*mod_name, name)) {
            errprintf (error, "mod_name %s != name %s", *mod_name, name);
            errno = EINVAL;
            goto error;
        }
    }
    *mod_mainp = mod_main;
    return dso;
error:
    ERRNO_SAFE_WRAP (dlclose, dso);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
