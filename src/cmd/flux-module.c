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
#include <stdio.h>
#include <getopt.h>
#include <assert.h>
#include <libgen.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <czmq.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"


#define OPTIONS "+h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    { 0, 0, 0, 0 },
};

static void module_list (flux_t h, int argc, char **argv);
static void module_remove (flux_t h, int argc, char **argv);
static void module_load (flux_t h, int argc, char **argv);

void usage (void)
{
    fprintf (stderr,
"Usage: flux-module list\n"
"       flux-module remove|rm module [module...]\n"
"       flux-module load module [arg=val...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *cmd;

    log_init ("flux-module");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!(h = flux_api_open ()))
        err_exit ("flux_api_open");

    if (!strcmp (cmd, "list"))
        module_list (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "rm") || !strcmp (cmd, "remove"))
        module_remove (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "load"))
        module_load (h, argc - optind, argv + optind);
    else
        usage ();

    flux_api_close (h);
    log_fini ();
    return 0;
}

static char *flagstr (int flags)
{
    char *s = xzmalloc (16);
    if ((flags & FLUX_MOD_FLAGS_MANAGED))
        strcat (s, "m");
    return s;
}

static char *idlestr (int idle)
{
    char *s;
    if (idle > 99)
        s = xstrdup ("idle");
    else if (asprintf (&s, "%d", idle) < 0)
        oom ();
    return s;
}

static void module_list_one (const char *key, JSON mo)
{
    const char *name, *nodelist = NULL;
    int flags, idle, size;
    char *fs, *is;

    if (!Jget_str (mo, "name", &name) || !Jget_int (mo, "flags", &flags)
     || !Jget_int (mo, "size", &size) || !Jget_str (mo, "nodelist", &nodelist)
     || !Jget_int (mo, "idle", &idle))
        msg_exit ("error parsing lsmod response");
    fs = flagstr (flags);
    is = idlestr (idle);
    printf ("%-20.20s %6d %-6s %4s %s\n", key, size, fs, is, nodelist);
    free (fs);
    free (is);
}

static void module_list (flux_t h, int argc, char **argv)
{
    JSON lsmod, mods;
    json_object_iter iter;

    if (argc > 0)
        usage ();
    if (flux_modctl_update (h) < 0)
        err_exit ("flux_modctl_update");
    /* FIXME: flux_modctl_update doesn't wait for KVS to be updated,
     * so there is a race here.  The following usleep should be removed
     * once this is addressed.
     */
    usleep (1000*100);
    printf ("%-20s %6s %-6s %4s %s\n",
            "Module", "Size", "Flags", "Idle", "Nodelist");
    if (kvs_get (h, "conf.modctl.lsmod", &lsmod) == 0) {
        if (!Jget_obj (lsmod, "mods", &mods))
            msg_exit ("error parsing lsmod KVS object");
        json_object_object_foreachC (mods, iter) {
            module_list_one (iter.key, iter.val);
        }
        Jput (lsmod);
    }
}

static void module_remove (flux_t h, int argc, char **argv)
{
    int i;
    char *key;

    if (argc == 0)
       usage ();
    for (i = 0; i < argc; i++) {
        if (asprintf (&key, "conf.modctl.modules.%s", argv[i]) < 0)
            oom ();
        if (kvs_unlink (h, key) < 0)
            err_exit ("%s", key);
        if (kvs_commit (h) < 0)
            err_exit ("kvs_commit");
        if (flux_modctl_rm (h, argv[i]) < 0)
            err_exit ("%s", argv[i]);
        msg ("%s: unloaded", argv[i]);
        free (key);
    }
}

static char *modname (const char *path)
{
    void *dso;
    char *s = NULL;
    const char **np;

    dlerror ();
    if (!(dso = dlopen (path, RTLD_NOW | RTLD_LOCAL))) {
        msg ("%s", dlerror ());
        goto done;
    }
    if (!(np = dlsym (dso, "mod_name")) || !*np) {
        msg ("%s: mod_name undefined", path);
        goto done;
    }
    s = xstrdup (*np);
done:
    if (dso)
        dlclose (dso);
    return s;
}

static char *modfind (const char *modpath, const char *name)
{
    char *cpy = xstrdup (modpath);
    char *path = NULL, *dir, *saveptr = NULL, *a1 = cpy;
    char *ret = NULL;

    while (!ret && (dir = strtok_r (a1, ":", &saveptr))) {
        if (asprintf (&path, "%s/%s.so", dir, name) < 0)
            oom ();
        if (access (path, R_OK|X_OK) < 0)
            free (path);
        else
            ret = path;
        a1 = NULL;
    }
    free (cpy);
    if (!ret)
        errno = ENOENT;
    return ret;
}

static int read_all (int fd, uint8_t **bufp)
{
    const int chunksize = 4096;
    int len = 0;
    uint8_t *buf = NULL;
    int n;
    int count = 0;

    do {
        if (len - count == 0) {
            len += chunksize;
            if (!(buf = buf ? realloc (buf, len) : malloc (len)))
                goto nomem;
        }
        if ((n = read (fd, buf + count, len - count)) < 0) {
            free (buf);
            return n;
        }
        count += n;
    } while (n != 0);
    *bufp = buf;
    return count;
nomem:
    errno = ENOMEM;
    return -1;
}

static JSON parse_modargs (int argc, char **argv)
{
    JSON args = Jnew ();
    int i;

    for (i = 0; i < argc; i++) {
        char *val = NULL, *cpy = xstrdup (argv[i]);
        if ((val = strchr (cpy, '=')))
            *val++ = '\0';
        if (!val)
            msg_exit ("malformed argument: %s", cpy);
        Jadd_str (args, cpy, val);
        free (cpy);
    }

    return args;
}

/* Copy mod to KVS (without commit).
 */
static void copymod (flux_t h, const char *name, const char *path, JSON args)
{
    JSON mod = Jnew ();
    char *key;
    int fd, len;
    uint8_t *buf;

    if (asprintf (&key, "conf.modctl.modules.%s", name) < 0)
        oom ();
    if (kvs_get (h, key, &mod) == 0)
        errn_exit (EEXIST, "%s", key);
    Jadd_obj (mod, "args", args);
    if ((fd = open (path, O_RDONLY)) < 0)
        err_exit ("%s", path);
    if ((len = read_all (fd, &buf)) < 0)
        err_exit ("%s", path);
    (void)close (fd);
    util_json_object_add_data (mod, "data", buf, len);
    if (kvs_put (h, key, mod) < 0)
        err_exit ("kvs_put %s", key);
    free (key);
    free (buf);
    Jput (mod);
}

static void module_load (flux_t h, int argc, char **argv)
{
    JSON args;
    char *path, *trypath = NULL;
    char *name;
    char *modpath = getenv ("FLUX_MODULE_PATH");

    if (argc == 0)
       usage ();
    path = argv[0];
    if (access (path, R_OK|X_OK) < 0) {
        if (!(trypath = modfind (modpath ? modpath : MODULE_PATH, path)))
            errn_exit (ENOENT, "%s", path);
        path = trypath;
    }
    if (!(name = modname (path)))
        exit (1);
    args = parse_modargs (argc - 1, argv + 1);
    copymod (h, name, path, args);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (flux_modctl_ins (h, name) < 0)
        err_exit ("flux_modctl_ins %s", name);
    msg ("module loaded");

    free (name);
    Jput (args);
    if (trypath)
        free (trypath);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
