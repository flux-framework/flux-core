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

void config_dump (flux_conf_t cf, int ac, char **av);
void config_get (flux_conf_t cf, int ac, char **av);
void config_put (flux_conf_t cf, flux_t h, bool vopt, int ac, char **av);
void config_save (flux_conf_t cf, bool vopt, int ac, char **av);

#define OPTIONS "hv"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"verbose",    no_argument,        0, 'v'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-config [OPTIONS] dump\n"
"       flux-config [OPTIONS] get key\n"
"       flux-config [OPTIONS] put key=val\n"
"       flux-config [OPTIONS] save [directory]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int ch;
    flux_conf_t cf;
    char *cmd;
    bool vopt = false;
    flux_t h = NULL;
    char *confdir = NULL;

    log_init ("flux-config");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'v': /* --verbose */
                vopt=  true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    /* Process config from the KVS if running in a session and not
     * forced to use a config file by the command line.
     */
    cf = flux_conf_create ();
    if ((confdir = getenv ("FLUX_CONF_DIRECTORY")))
        flux_conf_set_directory (cf, confdir);
    if (getenv ("FLUX_CONF_USEFILE")) {
        if (vopt)
            msg ("Loading config from %s", flux_conf_get_directory (cf));
        if (flux_conf_load (cf) < 0)
            err_exit ("%s", flux_conf_get_directory (cf));
    } else if (getenv ("FLUX_TMPDIR")) {
        if (vopt)
            msg ("Loading config from KVS");
        if (!(h = flux_api_open ()))
            err_exit ("flux_api_open");
        if (kvs_conf_load (h, cf) < 0)
            err_exit ("could not load config from KVS");
    }

    if (!strcmp (cmd, "get"))
        config_get (cf, argc - optind, argv + optind);
    else if (!strcmp (cmd, "dump"))
        config_dump (cf, argc - optind, argv + optind);
    else if (!strcmp (cmd, "put"))
        config_put (cf, h, vopt, argc - optind, argv + optind);
    else if (!strcmp (cmd, "save"))
        config_save (cf, vopt, argc - optind, argv + optind);
    else
        usage ();

    if (h)
        flux_api_close (h);
    flux_conf_destroy (cf);
    log_fini();
    exit (0);
}

void config_dump (flux_conf_t cf, int ac, char **av)
{
    flux_conf_itr_t itr;
    const char *key, *val;
    if (ac > 0)
        msg_exit ("dump accepts no arguments");
    itr = flux_conf_itr_create (cf);
    while ((key = flux_conf_next (itr))) {
        if (!(val = flux_conf_get (cf, key)))
            err_exit ("%s", key);
        printf("%s=%s\n", key, val);
    }
    flux_conf_itr_destroy (itr);
}

void config_get (flux_conf_t cf, int ac, char **av)
{
    const char *val;
    if (ac != 1)
        msg_exit ("get accepts one argument");
    val = flux_conf_get (cf, av[0]);
    if (!val)
        err_exit ("%s", av[0]);
    printf ("%s\n", val);
}

void config_put (flux_conf_t cf, flux_t h, bool vopt, int ac, char **av)
{
    char *key, *val;

    if (ac != 1)
        msg_exit ("put accepts one key[=val] argument");
    key = xstrdup (av[0]);
    if ((val = strchr (key, '=')))
        *val++ = '\0';
    if (flux_conf_put (cf, key, val) < 0)
        err_exit ("flux_conf_put");
    free (key);

    if (h) {
        if (vopt)
            msg ("Saving config to KVS");
        if (kvs_conf_save (h, cf) < 0)
            err_exit ("could not save config to KVS");
    } else {
        if (vopt)
            msg ("Saving config to %s", flux_conf_get_directory (cf));
        if (flux_conf_save (cf) < 0)
            err_exit ("%s", flux_conf_get_directory (cf));
    }
}

void config_save (flux_conf_t cf, bool vopt, int ac, char **av)
{
    if (ac > 1)
        msg_exit ("save accepts one optional argument");
    if (ac == 1)
        flux_conf_set_directory (cf, av[0]);
    if (vopt)
        msg ("Saving config to %s", flux_conf_get_directory (cf));
    if (flux_conf_save (cf) < 0)
        err_exit ("%s", flux_conf_get_directory (cf));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
