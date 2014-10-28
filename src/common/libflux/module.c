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
#include <dlfcn.h>

#include "module.h"
#include "request.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

/* Who will load modname?
 */
static char *mod_target (const char *modname)
{
    char *target = NULL;
    if (strchr (modname, '.')) {
        target = xstrdup (modname);
        char *p = strrchr (target, '.');
        *p = '\0';
    } else
        target = xstrdup ("cmb");
    return target;
}

#ifndef TEST_MAIN /* Not testing this section */

int flux_rmmod (flux_t h, int rank, const char *name, int flags)
{
    JSON request = Jnew ();
    JSON response = NULL;
    char *target = mod_target (name);
    int rc = -1;

    Jadd_str (request, "name", name);
    Jadd_int (request, "flags", flags);
    if ((response = flux_rank_rpc (h, rank, request, "%s.rmmod", target))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    free (target);
    Jput (request);
    Jput (response);
    return rc;
}

JSON flux_lsmod (flux_t h, int rank, const char *target)
{
    JSON request = Jnew ();
    JSON response = NULL;

    if (target == NULL)
        target = "cmb";
    response = flux_rank_rpc (h, rank, request, "%s.lsmod", target);
    Jput (request);
    return response;
}

int flux_insmod (flux_t h, int rank, const char *path, int flags, JSON args)
{
    JSON request = Jnew ();
    JSON response = NULL;
    char *name = NULL;
    char *target = NULL;
    int rc = -1;

    if (!(name = flux_modname (path))) {
        errno = EINVAL;
        goto done;
    }
    target = mod_target (name);
    Jadd_str (request, "path", path);
    Jadd_int (request, "flags", flags);
    Jadd_obj (request, "args", args);
    if ((response = flux_rank_rpc (h, rank, request, "%s.insmod", target))) {
        errno = EPROTO;
        goto done;
    }
    if (errno != 0)
        goto done;
    rc = 0;
done:
    if (target)
        free (target);
    Jput (request);
    Jput (response);
    return rc;
}

#endif /* !TEST_MAIN */

char *flux_modname(const char *path)
{
    void *dso;
    const char **np;
    char *name = NULL;

    dlerror ();
    if ((dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL))) {
        if ((np = dlsym (dso, "mod_name")) && *np)
            name = xstrdup (*np);
        dlclose (dso);
    }
    return name;
}

static int flux_modname_cmp(const char *path, const char *name)
{
    void *dso;
    const char **np;
    int rc = -1;

    dlerror ();
    if ((dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL))) {
        if ((np = dlsym (dso, "mod_name")) && *np)
            rc = strcmp (*np, name);
        dlclose (dso);
    }
    return rc;
}

#undef  MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))

static int strcmpend (const char *s1, const char *s2)
{
    int skip = MAX (strlen (s1) - strlen (s2), 0);
    return strcmp (s1 + skip, s2);
}

static char *modfind (const char *dirpath, const char *modname)
{
    DIR *dir;
    struct dirent entry, *dent;
    char *modpath = NULL;
    struct stat sb;
    char path[PATH_MAX];
    size_t len = sizeof (path);

    if (!(dir = opendir (dirpath)))
        goto done;
    while (!modpath) {
        if ((errno = readdir_r (dir, &entry, &dent)) > 0 || dent == NULL)
            break;
        if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
            continue;
        if (snprintf (path, len, "%s/%s", dirpath, dent->d_name) >= len) {
            errno = EINVAL;
            break;
        }
        if (stat (path, &sb) == 0) {
            if (S_ISDIR (sb.st_mode))
                modpath = modfind (path, modname);
            else if (!strcmpend (path, ".so")) {
                if (!flux_modname_cmp (path, modname))
                    modpath = xstrdup (path);
            }
        }
    }
    closedir (dir);
done:
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

#ifdef TEST_MAIN

#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    char *name, *path;

    plan (16);

    ok ((strcmpend ("foo.so", ".so") == 0),
        "strcmpend matches .so");
    ok ((strcmpend ("", ".so") != 0),
        "strcmpend doesn't match empty string");

    name = mod_target ("kvs");
    like (name, "^cmb$",
        "mod_target of kvs is cmb");
    if (name)
        free (name);

    name = mod_target ("sched.backfill");
    like (name, "^sched$",
        "mod_target of sched.backfill is sched");
    if (name)
        free (name);

    name = mod_target ("sched.backfill.priority");
    like (name, "^sched.backfill$",
        "mod_target of sched.backfill.priority is sched.backfill");
    if (name)
        free (name);

    path = xasprintf ("%s/kvs/.libs/kvs.so", MODULE_PATH);
    ok (access (path, F_OK) == 0,
        "built kvs module is located");
    name = flux_modname (path);
    ok ((name != NULL),
        "flux_modname on kvs should find a name");
    skip (name == NULL, 1,
        "skip next test because kvs.so name is NULL");
    like (name, "^kvs$",
        "flux_modname says kvs module is named kvs");
    end_skip;
    if (name)
        free (name);
    ok (flux_modname_cmp (name, "kvs"),
        "flux_modname_cmp also says kvs module is named kvs");
    free (path);

    ok (!modfind ("nowhere", "foo"),
        "modfind fails with nonexistent directory");
    ok (!modfind (".", "foo"),
        "modfind fails in current directory");
    ok (!modfind (MODULE_PATH, "foo"),
        "modfind fails to find unknown module in moduledir");

    path = xasprintf ("%s/kvs/.libs", MODULE_PATH);
    name = modfind (path, "kvs");
    ok ((name != NULL),
        "modfind finds kvs in flat directory");
    if (name)
        free (name);
    free (path);

    name = modfind (MODULE_PATH, "kvs");
    ok ((name != NULL),
        "modfind finds kvs in deep moduledir");
    if (name)
        free (name);

    name = flux_modfind (MODULE_PATH, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in moduledir");
    if (name)
        free (name);

    path = xasprintf ("foo:bar:xyz:%s:zzz", MODULE_PATH);
    name = flux_modfind (path, "kvs");
    ok ((name != NULL),
        "flux_modfind also finds kvs in search path");
    if (name)
        free (name);
    free (path);

    done_testing ();
}

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
