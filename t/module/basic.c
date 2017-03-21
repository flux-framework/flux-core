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
#include <flux/core.h>
#include <dlfcn.h>

#include "src/common/libutil/log.h"

typedef struct {
    uint32_t nodeid;
    int argc;
    char **argv;
} opt_t;

#define OPTIONS "+hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void mod_lsmod (flux_t *h, opt_t opt);
void mod_rmmod (flux_t *h, opt_t opt);
void mod_insmod (flux_t *h, opt_t opt);

typedef struct {
    const char *name;
    void (*fun)(flux_t *h, opt_t opt);
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
"Usage: module-basic list   [OPTIONS]\n"
"       module-basic load   [OPTIONS] module [arg ...]\n"
"       module-basic remove [OPTIONS] module\n"
"where OPTIONS are:\n"
"       -r,--rank=NODESET     add ranks (default \"0\") \n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    int ch;
    char *cmd;
    func_t *f;
    opt_t opt;

    log_init ("module-basic");

    memset (&opt, 0, sizeof (opt));
    if (argc < 2)
        usage ();
    cmd = argv[1];
    argc--;
    argv++;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank=NODESET */
                opt.nodeid = strtoul (optarg, NULL, 0);
                break;
            default:
                usage ();
                break;
        }
    }
    opt.argc = argc - optind;
    opt.argv = argv + optind;

    if (!(f = func_lookup (cmd)))
        log_msg_exit ("unknown function '%s'", cmd);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    f->fun (h, opt);

    flux_close (h);
    log_fini ();
    return 0;
}

void parse_modarg (const char *arg, char **name, char **path)
{
    char *modpath = NULL;
    char *modname = NULL;

    if (strchr (arg, '/')) {
        if (!(modpath = realpath (arg, NULL)))
            log_err_exit ("%s", arg);
        if (!(modname = flux_modname (modpath)))
            log_msg_exit ("%s", dlerror ());
    } else
        log_msg_exit ("must specify absolute path");
    *name = modname;
    *path = modpath;
}

void mod_insmod (flux_t *h, opt_t opt)
{
    char *modname;
    char *modpath;

    if (opt.argc < 1)
        usage ();
    parse_modarg (opt.argv[0], &modname, &modpath);
    opt.argv++;
    opt.argc--;

    if (flux_insmod (h, opt.nodeid, modpath, opt.argc, opt.argv) < 0)
        log_err_exit ("flux_insmod");

    free (modpath);
    free (modname);
}

void mod_rmmod (flux_t *h, opt_t opt)
{
    char *modname = NULL;

    if (opt.argc != 1)
        usage ();
    modname = opt.argv[0];

    if (flux_rmmod (h, opt.nodeid, modname) < 0)
        log_err_exit ("flux_rmmod");
}

int lsmod_cb (const char *name, int size, const char *digest,
              int idle, int status, const char *nodeset, void *arg)
{
    printf ("Module: %s\n", name);
    return (0);
}

void mod_lsmod (flux_t *h, opt_t opt)
{
    if (opt.argc > 1)
        usage ();

    if (flux_lsmod (h, opt.nodeid, NULL, lsmod_cb, NULL) < 0)
        log_err_exit ("flux_lsmod");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
