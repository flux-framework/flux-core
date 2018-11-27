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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <dlfcn.h>
#include <argz.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>

#include "module.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/dirwalk.h"

char *flux_modname(const char *path)
{
    void *dso;
    const char **np;
    char *name = NULL;

    dlerror ();
    if ((dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL | FLUX_DEEPBIND))) {
        int errnum = EINVAL;
        if ((np = dlsym (dso, "mod_name")) && *np)
            if (!(name = strdup (*np)))
                errnum = ENOMEM;
        dlclose (dso);
        errno = errnum;
        return name;
    }
    // Another reporting method may be warranted here, but when a dynamic
    // library dependency doesn't resolve, it really helps to know that's
    // the error.  Otherwise it prints as "invalid argument" from the
    // broker.
    log_msg ("%s", dlerror ());
    errno = ENOENT;
    return NULL;
}

/* helper for flux_modfind() */
static int flux_modname_cmp(const char *path, const char *name)
{
    char * modname = flux_modname(path);
    int rc = modname ? strcmp(modname, name) : -1;
    free(modname);
    return rc;
}

/* helper for flux_modfind() */
static int mod_find_f (dirwalk_t *d, void *arg)
{
    const char *name = arg;
    return (flux_modname_cmp (dirwalk_path (d), name) == 0);
}

char *flux_modfind (const char *searchpath, const char *modname)
{
    char *result = NULL;
    zlist_t *l;

    if (!searchpath || !modname) {
        errno = EINVAL;
        return NULL;
    }
    l = dirwalk_find (searchpath, 0, "*.so", 1, mod_find_f, (void *) modname);
    if (l) {
        result = zlist_pop (l);
        zlist_destroy (&l);
    }
    if (!result)
        errno = ENOENT;
    return result;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
