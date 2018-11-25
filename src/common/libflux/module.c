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

struct flux_modlist_struct {
    json_t *o;
};

int flux_modlist_get (flux_modlist_t *mods, int n, const char **name, int *size,
                      const char **digest, int *idle, int *status)
{
    json_t *a, *o;
    int rc = -1;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)
                                        || !(o = json_array_get (a, n))) {
        errno = EPROTO;
        goto done;
    }
    if (json_unpack (o, "{s:s, s:i, s:s, s:i, s:i}", "name", name,
                    "size", size, "digest", digest, "idle", idle,
                    "status", status) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_modlist_count (flux_modlist_t *mods)
{
    json_t *a;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)) {
        errno = EPROTO;
        return -1;
    }
    return json_array_size (a);
}

int flux_modlist_append (flux_modlist_t *mods, const char *name, int size,
                            const char *digest, int idle, int status)
{
    json_t *a, *o;
    int rc = -1;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)) {
        errno = EPROTO;
        goto done;
    }
    if (!(o = json_pack ("{s:s, s:i, s:s, s:i, s:i}", "name", name,
                         "size", size, "digest", digest, "idle", idle,
                          "status", status))) {
        errno = ENOMEM;
        goto done;
    }
    if (json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void flux_modlist_destroy (flux_modlist_t *mods)
{
    if (mods) {
        json_decref (mods->o);
        free (mods);
    }
}

flux_modlist_t *flux_modlist_create (void)
{
    flux_modlist_t *mods = calloc (1, sizeof (*mods));
    if (!mods || !(mods->o = json_array ())
              || !(mods->o = json_pack ("{s:o}", "mods", mods->o))) {
        flux_modlist_destroy (mods);
        errno = ENOMEM;
        return NULL;
    }
    return mods;
}

char *flux_lsmod_json_encode (flux_modlist_t *mods)
{
    return json_dumps (mods->o, JSON_COMPACT);
}

flux_modlist_t *flux_lsmod_json_decode (const char *json_str)
{
    flux_modlist_t *mods = calloc (1, sizeof (*mods));
    json_error_t error;
    if (!mods)
        return NULL;
    if (!(mods->o = json_loads (json_str, 0, &error))) {
        free (mods);
        errno = EPROTO;
        return NULL;
    }
    return mods;
}

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
    zlist_t *l = dirwalk_find (searchpath, 0, "*.so", 1,
                               mod_find_f, (void *) modname);
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
