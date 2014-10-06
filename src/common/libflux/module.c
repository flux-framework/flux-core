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
#include "module.h"
#include "request.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"


int flux_rmmod (flux_t h, int rank, const char *name, int flags)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "name", name);
    Jadd_int (request, "flags", flags);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.rmmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

JSON flux_lsmod (flux_t h, int rank)
{
    JSON request = Jnew ();
    JSON response = NULL;

    response = flux_rank_rpc (h, rank, request, "cmb.lsmod");
    Jput (request);
    return response;
}

int flux_insmod (flux_t h, int rank, const char *path, int flags, JSON args)
{
    JSON request = Jnew ();
    JSON response = NULL;
    int rc = -1;

    Jadd_str (request, "path", path);
    Jadd_int (request, "flags", flags);
    Jadd_obj (request, "args", args);
    if ((response = flux_rank_rpc (h, rank, request, "cmb.insmod"))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return rc;
}

#include <glob.h>
#include <dlfcn.h>

char *flux_modname(const char *path)
{
    void *dso;
    const char **np;
    char *name = NULL;

    dlerror ();
    if ((dso = dlopen (path, RTLD_NOW | RTLD_LOCAL))) {
        if ((np = dlsym (dso, "mod_name")) && *np)
            name = xstrdup (*np);
        dlclose (dso);
    }
    return name;
}

static char *modfind (const char *dirpath, const char *modname)
{
    glob_t gl;
    char *globstr;
    int i;
    char *modpath = NULL;

    if (asprintf (&globstr, "%s/*.so", dirpath) < 0)
        oom ();
    if (glob (globstr, 0, NULL, &gl) == 0) {
        for (i = 0; i < gl.gl_pathc && !modpath; i++) {
            char *name = flux_modname (gl.gl_pathv[i]);
            if (name) {
                if (!strcmp (name, modname))
                    modpath = xstrdup (gl.gl_pathv[i]);
                free (name);
            }
        }
        globfree (&gl);
    }
    free (globstr);
    return modpath;
}

char *flux_modfind (const char *searchpath, const char *modname)
{
    char *cpy = xstrdup (searchpath);
    char *dirpath, *saveptr = NULL, *a1 = cpy;
    char *modpath = NULL;

    while ((dirpath = strtok_r (a1, ":", &saveptr))) {
        if ((modpath = modfind (dirpath, modname)))
            break;
        a1 = NULL;
    }
    free (cpy);
    if (!modpath)
        errno = ENOENT;
    return modpath;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
