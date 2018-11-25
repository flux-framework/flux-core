/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
    FLUX_MODSTATE_INIT           = 0,
    FLUX_MODSTATE_SLEEPING       = 1,
    FLUX_MODSTATE_RUNNING        = 2,
    FLUX_MODSTATE_FINALIZING     = 3,
    FLUX_MODSTATE_EXITED         = 4,
};

/*
 * Mandatory symbols for modules
 */
#define MOD_NAME(x) const char *mod_name = x
typedef int (mod_main_f)(flux_t *h, int argc, char *argv[]);


/* Read the value of 'mod_name' from the specified module filename.
 * Caller must free the returned name.  Returns NULL on failure.
 */
char *flux_modname (const char *filename);

/* Search a colon-separated list of directories (recursively) for a .so file
 * with the requested module name and return its path, or NULL on failure.
 * Caller must free the returned path.
 */
char *flux_modfind (const char *searchpath, const char *modname);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
