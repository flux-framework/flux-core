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

#include "src/modules/modctl/modctl.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/readall.h"

const int max_idle = 99;


#define OPTIONS "+hr:an:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"all",        no_argument,        0, 'a'},
    {"nodeset",    required_argument,  0, 'n'},
    { 0, 0, 0, 0 },
};

void mod_lsmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av);
void mod_rmmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av);
void mod_insmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av);

typedef struct {
    const char *name;
    void (*fun)(flux_t h, uint32_t nodeid, const char *ns, int ac, char **av);
} func_t;

static func_t funcs[] = {
    { "list",   &mod_lsmod},
    { "remove", &mod_rmmod},
    { "load",   &mod_insmod},
};

func_t *func_lookup (const char *name)
{
    int i;
    for (i = 0; i < sizeof (funcs) / sizeof (funcs[0]); i++)
        if (!strcmp (funcs[i].name, name))
            return &funcs[i];
    return NULL;
}

void usage (void)
{
    fprintf (stderr,
"Usage: flux-module [OPTIONS] list [service]\n"
"       flux-module [OPTIONS] remove module\n"
"       flux-module [OPTIONS] load module [arg ...]\n"
"where OPTIONS are:\n"
"       -r,--rank N      specify nodeid to send request\n"
"       -n,--nodeset NS  use modctl to distribute request to NS\n"
"       -a,--all         use modctl to distribute request to entire session\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *cmd;
    func_t *f;
    uint32_t nodeid = FLUX_NODEID_ANY;
    char *nodeset = NULL;
    bool aopt = false;

    log_init ("flux-module");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            case 'a': /* --all */
                aopt = true;
                break;
            case 'n': /* --nodeset NS */
                nodeset = xstrdup (optarg);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];
    if (!(f = func_lookup (cmd)))
        msg_exit ("unknown function '%s'", cmd);

    if (!(h = flux_api_open ()))
        err_exit ("flux_api_open");
    if (!nodeset && aopt) {
        int size = flux_size (h);
        nodeset = size > 1 ? xasprintf ("[0-%d]", size - 1) : xstrdup ("0");
    }
    f->fun (h, nodeid, nodeset, argc - optind, argv + optind);
    flux_api_close (h);

    if (nodeset)
        free (nodeset);
    log_fini ();
    return 0;
}

void mod_insmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (ac < 1)
        usage ();
    if (strchr (av[0], '/')) {                /* path name given */
        modpath = xstrdup (av[0]);
        if (!(modname = flux_modname (modpath)))
            msg_exit ("%s", dlerror ());
    } else {
        char *searchpath = getenv ("FLUX_MODULE_PATH");
        if (!searchpath)
            searchpath = MODULE_PATH;
        modname = xstrdup (av[0]);
        if (!(modpath = flux_modfind (searchpath, modname)))
            msg_exit ("%s: not found in module search path", modname);
    }
    if (ns) {
        if (flux_modctl_load (h, ns, modpath, ac - 1, av + 1) < 0)
            err_exit ("%s", av[0]);
    } else {
        if (flux_insmod (h, nodeid, modpath, ac - 1, av + 1) < 0)
            err_exit ("%s", av[0]);
    }
    if (modpath)
        free (modpath);
    if (modname)
        free (modname);
}

void mod_rmmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av)
{
    char *modname = NULL;

    if (ac != 1)
        usage ();
    modname = av[0];
    if (ns) {
        if (flux_modctl_unload (h, ns, modname) < 0)
            err_exit ("modctl_unload %s", modname);
    } else {
        if (flux_rmmod (h, nodeid, modname) < 0)
            err_exit ("%s", av[0]);
    }
}

const char *snip (const char *s, int n)
{
    if (strlen (s) < n)
        return s;
    else
        return s + strlen (s) - n;
}

static int lsmod_cb (const char *name, int size, const char *digest, int idle,
                     const char *nodeset, void *arg)
{
    char idle_str[16];
    if (idle < 100)
        snprintf (idle_str, sizeof (idle_str), "%d", idle);
    else
        strncpy (idle_str, "idle", sizeof (idle_str));
    printf ("%-20.20s %6d %7s %4s %s\n",
            name,
            size,
            snip (digest, 7),
            idle_str,
            nodeset ? nodeset : "");
    return 0;
}

void mod_lsmod (flux_t h, uint32_t nodeid, const char *ns, int ac, char **av)
{
    char *svc = "cmb";

    if (ac > 1)
        usage ();
    if (ac == 1)
        svc = av[0];
    printf ("%-20s %6s %7s %4s %s\n",
            "Module", "Size", "Digest", "Idle", "Nodeset");
    if (ns) {
        if (flux_modctl_list (h, svc, ns, lsmod_cb, NULL) < 0)
            err_exit ("modctl_list");
    } else {
        if (flux_lsmod (h, nodeid, svc, lsmod_cb, NULL) < 0)
            err_exit ("%s", svc);
    }
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
