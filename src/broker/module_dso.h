/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_MODULE_DSO_H
#define _BROKER_MODULE_DSO_H

#include <flux/core.h>

#include "module.h"

/* Search 'searchpath', a colon-separated list of directories, for a file
 * whose path matches the pattern "name.so*".  Return its full path (caller
 * must free).
 */
char *module_dso_search (const char *name,
                         const char *searchpath,
                         flux_error_t *error);

/* dlopen(3) the DSO at path and fetch a pointer to a symbol named 'mod_main'.
 * Optional:  if name is set and 'mod_name' is defined in the dso, fail if
 * they do not match.  This is a sanity check that modules still using the
 * deprecated mod_name symbol are at least setting it to the expected value.
 * The open DSO is returned.  Call module_dso_close() when done with it.
 */
void *module_dso_open (const char *path,
                       const char *name,
                       mod_main_f *mod_mainp,
                       flux_error_t *error);

/* Call dlclose(3) if not using the address sanitizer.  Preserve errno.
 */
void module_dso_close (void *dso);

/* Guess the broker module's name based on its path.
 * Caller must free the returned string.
 */
char *module_dso_name (const char *path);

#endif /* !_BROKER_MODULE_DSO_H */

// vi:ts=4 sw=4 expandtab
