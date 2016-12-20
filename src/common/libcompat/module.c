/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <flux/core.h>
#include "src/common/libutil/xzmalloc.h"
#include "compat.h"


char *flux_modfind (const char *searchpath, const char *modname)
{
    char *path = NULL;
    flux_module_t *m;
    flux_extensor_t *e = flux_extensor_create ();
    if (e && (m = flux_extensor_load_module (e, searchpath, modname)))
        path = strdup (flux_module_path (m));
    flux_extensor_destroy (e);
    return (path);
}

char *flux_modname (const char *path)
{
    char *name = NULL;
    flux_module_t *m;
    flux_extensor_t *e = flux_extensor_create ();
    if (!e)
        return (NULL);
    if ((m = flux_module_create (e, path, 0)) && flux_module_load (m) >= 0)
        name = strdup (flux_module_name (m));
    flux_extensor_destroy (e);
    return (name);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
