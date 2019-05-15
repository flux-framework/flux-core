/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MODULE_H
#define _FLUX_CORE_MODULE_H

/* Module management messages are constructed according to Flux RFC 5.
 * https://github.com/flux-framework/rfc/blob/master/spec_5.adoc
 */

#include <stdint.h>

#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Module states, for embedding in keepalive messages (rfc 5)
 */
enum {
    FLUX_MODSTATE_INIT = 0,
    FLUX_MODSTATE_SLEEPING = 1,
    FLUX_MODSTATE_RUNNING = 2,
    FLUX_MODSTATE_FINALIZING = 3,
    FLUX_MODSTATE_EXITED = 4,
};

/* Mandatory symbols for modules
 */
#define MOD_NAME(x) const char *mod_name = x
typedef int(mod_main_f) (flux_t *h, int argc, char *argv[]);

typedef void(flux_moderr_f) (const char *errmsg, void *arg);

/* Read the value of 'mod_name' from the specified module filename.
 * Caller must free the returned name.  Returns NULL on failure.
 * If 'cb' is non-NULL, any dlopen/dlsym errors are reported via callback.
 */
char *flux_modname (const char *filename, flux_moderr_f *cb, void *arg);

/* Search a colon-separated list of directories (recursively) for a .so file
 * with the requested module name and return its path, or NULL on failure.
 * Caller must free the returned path.
 * If 'cb' is non-NULL, any dlopen/dlsym errors are reported via callback.
 */
char *flux_modfind (const char *searchpath,
                    const char *modname,
                    flux_moderr_f *cb,
                    void *arg);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
