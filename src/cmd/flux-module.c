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
#include <dlfcn.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/readall.h"


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
"       flux-module remove module\n"
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

    if (!strcmp (cmd, "list") || !strcmp (cmd, "ls"))
        module_list (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "remove") || !strcmp (cmd, "rm"))
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
    char *key, *mod;

    if (argc != 1)
       usage ();
    mod = argv[0];
    if (asprintf (&key, "conf.modctl.modules.%s", mod) < 0)
        oom ();
    if (kvs_unlink (h, key) < 0)
        err_exit ("%s", key);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (flux_modctl_rm (h, mod) < 0)
        err_exit ("%s", mod);
    msg ("%s: unloaded", mod);
    free (key);
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
    char *path, *name;
    char *searchpath = getenv ("FLUX_MODULE_PATH");

    if (!searchpath)
        searchpath = MODULE_PATH;

    if (argc == 0)
       usage ();
    if (strchr (argv[0], '/')) {                /* path name given */
        path = xstrdup (argv[0]);
        if (!(name = flux_modname (path)))
            msg_exit ("%s", dlerror ());
    } else {                                    /* module name given */
        name = xstrdup (argv[0]);
        if (!(path = flux_modfind (searchpath, name)))
            msg_exit ("%s: not found in module search path", name);
    }
    argc--;
    argv++;

    args = parse_modargs (argc, argv);
    copymod (h, name, path, args);
    if (kvs_commit (h) < 0)
        err_exit ("kvs_commit");
    if (flux_modctl_ins (h, name) < 0)
        err_exit ("flux_modctl_ins %s", name);
    msg ("module loaded");

    Jput (args);
    free (name);
    free (path);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
