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
#include <sys/types.h>
#include <getopt.h>
#include <pwd.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/argv.h"
#include "src/common/libutil/log.h"

void config_dump (const char *config_file, int ac, char **av);
void config_get (const char *config_file, int ac, char **av);
void config_put (const char *config_file, int ac, char **av);

#define OPTIONS "hc:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"config",     required_argument,  0, 'c'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-config [-c FILE] dump [key]\n"
"       flux-config [-c FILE] get key\n"
"       flux-config [-c FILE] put key=val\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    char *config_file = getenv ("FLUX_CONFIG");
    char *cmd;

    log_init ("flux-config");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'c': /* --config FILE */
                config_file = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];
    if (!strcmp (cmd, "get"))
        config_get (config_file, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dump"))
        config_dump (config_file, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put"))
        config_put (config_file, argc - optind, argv + optind);
    else
        usage ();

    exit (0);
}

static char *mktab (int level)
{
    const int tabspace = 4;
    char *tab = xzmalloc (level*tabspace + 1);
    memset (tab, ' ', level*tabspace);
    return tab;
}

void zdump (zconfig_t *z, int level)
{
    char *tab = mktab (level);
    char *key = zconfig_name (z);
    char *val = zconfig_value (z);
    zconfig_t *child = zconfig_child (z);

    if (child) {
        printf ("%s%s:\n", tab, key);
        do {
            zdump (child, level + 1);
            child = zconfig_next (child);
        } while (child);
    } else if (val)
        printf ("%s%s = \"%s\"\n", tab, key, val);
    else
        printf ("%s%s = nil\n", tab, key);

    free (tab);
}

void config_dump (const char *config_file, int ac, char **av)
{
    zconfig_t *root, *z;
    char *key = "/";

    if (ac > 1)
        msg_exit ("dump accepts zero or one argument");
    if (ac == 1)
        key = av[0];
    root = flux_config_load (config_file, true);
    if (!strcmp (key, "/") || !strcmp (key, "root")) {
        z = zconfig_child (root);
        while (z) {
            zdump (z, 0);
            z = zconfig_next (z);
        }
    } else {
        if (!(z = zconfig_locate (root, key)))
            errn_exit (ENOENT, "%s", key);
        zdump (z, 0);
    }
    zconfig_destroy (&root);
}

void config_get (const char *config_file, int ac, char **av)
{
    zconfig_t *root, *z;
    char *key, *val;

    if (ac != 1)
        msg_exit ("get accepts one argument");
    key = av[0];
    root = flux_config_load (config_file, true);
    if (!(z = zconfig_locate (root, key)) || !(val = zconfig_value (z))
                                          || strlen (val) == 0)
        errn_exit (ENOENT, "%s", key);
    printf ("%s\n", val);
    zconfig_destroy (&root);
}

void config_put (const char *config_file, int ac, char **av)
{
    zconfig_t *root;
    char *key, *val;

    if (ac != 1)
        msg_exit ("put accepts one key[=val] argument");
    key = xstrdup (av[0]);
    if ((val = strchr (key, '=')))
        *val++ = '\0';
    root = flux_config_load (config_file, false);
    zconfig_put (root, key, val);
    flux_config_save (config_file, root);
    zconfig_destroy (&root);
    free (key);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
