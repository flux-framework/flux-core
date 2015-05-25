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
#include <argz.h>

#include "module.h"
#include "message.h"

#include "src/common/libjsonc/jsonc.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

/* Get service name from module name string.
 */
static char *mod_service (const char *modname)
{
    char *service = NULL;
    if (strchr (modname, '.')) {
        service = xstrdup (modname);
        char *p = strrchr (service, '.');
        *p = '\0';
    } else
        service = xstrdup ("cmb");
    return service;
}

/**
 ** JSON encode/decode functions
 **/

int flux_insmod_json_decode (JSON o, char **path, char **argz, size_t *argz_len)
{
    JSON args = NULL;
    const char *s;
    int i, ac;
    int rc = -1;

    if (!Jget_str (o, "path", &s) || !Jget_obj (o, "args", &args)
                                  || !Jget_ar_len (args, &ac)) {
        errno = EPROTO;
        goto done;
    }
    *path = xstrdup (s);
    for (i = 0; i < ac; i++) {
        (void)Jget_ar_str (args, i, &s); /* can't fail? */
        argz_add (argz, argz_len, s);
    }
    rc = 0;
done:
    Jput (args);
    return rc;
}

JSON flux_insmod_json_encode (const char *path, int argc, char **argv)
{
    JSON o = Jnew ();
    JSON args = Jnew_ar ();
    int i;

    Jadd_str (o, "path", path);
    for (i = 0; i < argc; i++)
        Jadd_ar_str (args, argv[i]);
    Jadd_obj (o, "args", args);
    return o;
}

int flux_rmmod_json_decode (JSON o, char **name)
{
    const char *s;
    int rc = -1;
    if (!Jget_str (o, "name", &s)) {
        errno = EPROTO;
        goto done;
    }
    *name = xstrdup (s);
    rc = 0;
done:
    return rc;
}

JSON flux_rmmod_json_encode (const char *name)
{
    JSON o = Jnew ();
    Jadd_str (o, "name", name);
    return o;
}

int flux_lsmod_json_decode_nth (JSON a, int n, const char **name, int *size,
                                const char **digest, int *idle)
{
    JSON o;
    int rc = -1;

    if (!Jget_ar_obj (a, n, &o) || !Jget_str (o, "name", name)
                                || !Jget_int (o, "size", size)
                                || !Jget_str (o, "digest", digest)
                                || !Jget_int (o, "idle", idle)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_lsmod_json_decode (JSON a, int *len)
{
    int rc = -1;

    if (!Jget_ar_len (a, len)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_lsmod_json_append (JSON a, const char *name, int size,
                            const char *digest, int idle)
{
    JSON o = Jnew ();
    Jadd_str (o, "name", name);
    Jadd_int (o, "size", size);
    Jadd_str (o, "digest", digest);
    Jadd_int (o, "idle", idle);
    Jadd_ar_obj (a, o); /* takes a ref on o */
    Jput (o);
    return 0;
}

JSON flux_lsmod_json_create (void)
{
    return Jnew_ar ();
}

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

/* helper for flux_modfind() */
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

#ifndef MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))
#endif

/* helper for flux_modfind() */
static int strcmpend (const char *s1, const char *s2)
{
    int skip = MAX (strlen (s1) - strlen (s2), 0);
    return strcmp (s1 + skip, s2);
}

/* helper for flux_modfind() */
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
                    if (!(modpath = realpath (path, NULL)))
                        oom ();

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

int flux_insmod_request_decode (zmsg_t *zmsg, char **path,
                                char **argz, size_t *argz_len)
{
    JSON in = NULL;
    int rc = -1;

    if (flux_json_request_decode (zmsg, &in) < 0)
        goto done;
    if (flux_insmod_json_decode (in, path, argz, argz_len) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

int flux_rmmod_request_decode (zmsg_t *zmsg, char **name)
{
    JSON in = NULL;
    int rc = -1;

    if (flux_json_request_decode (zmsg, &in) < 0)
        goto done;
    if (flux_rmmod_json_decode (in, name) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

int flux_lsmod_request_decode (zmsg_t *zmsg)
{
    int type;
    int rc = -1;

    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_REQUEST || flux_msg_has_payload (zmsg)) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_rmmod (flux_t h, uint32_t nodeid, const char *name)
{
    JSON in = NULL;
    char *service = mod_service (name);
    char *topic = xasprintf ("%s.rmmod", service);
    int rc = -1;

    in = flux_rmmod_json_encode (name);
    Jadd_str (in, "name", name);
    if (flux_json_rpc (h, nodeid, topic, in, NULL) < 0)
        goto done;
    rc = 0;
done:
    free (service);
    free (topic);
    Jput (in);
    return rc;
}

int flux_lsmod (flux_t h, uint32_t nodeid, const char *service,
                flux_lsmod_f cb, void *arg)
{
    JSON out = NULL;
    char *topic = xasprintf ("%s.lsmod", service ? service : "cmb");
    int rc = -1;
    int i, len;

    if (flux_json_rpc (h, nodeid, topic, NULL, &out) < 0)
        goto done;
    if (flux_lsmod_json_decode (out, &len) < 0)
        goto done;
    for (i = 0; i < len; i++) {
        const char *name, *digest;
        int size, idle;
        if (flux_lsmod_json_decode_nth (out, i, &name, &size, &digest, &idle) < 0)
            goto done;
        if (cb (name, size, digest, idle, NULL, arg) < 0)
            goto done;
    }
    rc = 0;
done:
    free (topic);
    return rc;
}

int flux_insmod (flux_t h, uint32_t nodeid, const char *path,
                 int argc, char **argv)
{
    JSON in = Jnew ();
    char *name = NULL;
    char *service = NULL;
    char *topic = NULL;
    int rc = -1;

    if (!(name = flux_modname (path))) {
        errno = EINVAL;
        goto done;
    }
    service = mod_service (name);
    topic = xasprintf ("%s.insmod", service);

    in = flux_insmod_json_encode (path, argc, argv);
    if (flux_json_rpc (h, nodeid, topic, in, NULL) < 0)
        goto done;
    rc = 0;
done:
    if (service)
        free (service);
    if (topic)
        free (topic);
    Jput (in);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
