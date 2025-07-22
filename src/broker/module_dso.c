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
#include "src/common/libutil/dirwalk.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/basename.h"
#include "ccan/str/str.h"

#include "module_dso.h"

char *module_dso_search (const char *name,
                         const char *searchpath,
                         flux_error_t *error)
{
    char *pattern;
    zlist_t *files = NULL;
    char *path;

    if (asprintf (&pattern, "%s.so*", name) < 0) {
        errprintf (error, "out of memory");
        return NULL;
    }
    if (!(files = dirwalk_find (searchpath,
                                DIRWALK_REALPATH | DIRWALK_NORECURSE,
                                pattern,
                                1,
                                NULL,
                                NULL))
        || zlist_size (files) == 0) {
        errprintf (error, "module not found in search path");
        errno = ENOENT;
        goto error;
    }
    if (!(path = strdup (zlist_first (files))))
        goto error;
    zlist_destroy (&files);
    free (pattern);
    return path;
error:
    ERRNO_SAFE_WRAP (zlist_destroy, &files);
    ERRNO_SAFE_WRAP (free, pattern);
    return NULL;
};

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

char *module_dso_name (const char *path)
{
    char *name;
    char *cp;

    name = basename_simple (path);
    // if path ends in .so or .so.VERSION, strip it off
    if ((cp = strstr (name, ".so")))
        return strndup (name, cp - name);
    return strdup (name);
}

// vi:ts=4 sw=4 expandtab
