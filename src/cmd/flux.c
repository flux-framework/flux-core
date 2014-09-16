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

/* flux.c - Flux command front-end */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <stdbool.h>
#include <sys/param.h>
#include <json.h>
#include <glob.h>

#include "log.h"
#include "xzmalloc.h"
#include "setenvf.h"
#include "argv.h"

void dump_environment (void);
void exec_subcommand (bool vopt, char *argv[]);
char *dir_self (void);

#define OPTIONS "+T:tx:hM:B:v"
static const struct option longopts[] = {
    {"tmpdir",          required_argument,  0, 'T'},
    {"trace-apisocket", no_argument,        0, 't'},
    {"exec-path",       required_argument,  0, 'x'},
    {"module-path",     required_argument,  0, 'M'},
    {"cmbd-path",       required_argument,  0, 'B'},
    {"verbose",         no_argument,        0, 'v'},
    {"help",            no_argument,        0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, 
"Usage: flux [OPTIONS] COMMAND ARGS\n"
"    -x,--exec-path PATH      set FLUX_EXEC_PATH\n"
"    -M,--module-path PATH    set FLUX_MODULE_PATH\n"
"    -T,--tmpdir PATH         set FLUX_TMPDIR\n"
"    -B,--trace-apisock       set FLUX_TRACE_APISOCK=1\n"
"    -t,--trace-apisock       set FLUX_TRACE_APISOCK=1\n"
"    -B,--cmbd-path           set FLUX_CMBD_PATH\n"
"    -v,--verbose             show environment before executing command\n"
);
}

static void help (void)
{
    usage ();
    fprintf (stderr, "\nThe flux-core commands are:\n"
"   keygen        Generate CURVE keypairs for session security\n"
"   kvs           Access the Flux the key-value store\n"
"   module        Load/unload comms modules\n"
"   start-single  Start a single-rank comms session interactively\n"
"   start-screen  Start a single-node comms session under screen\n"
"   start-srun    Start a multi-node comms session under SLURM\n"
"   up            Show state of all broker ranks\n"
"   ping          Time round-trip RPC on the comms rank-request network\n"
"   mping         Time round-trip group RPC to the mecho comms module\n"
"   snoop         Snoop on local Flux message broker traffic\n"
"   event         Publish and subscribe to Flux events\n"
"   logger        Log a message to Flux logging system\n"
"   comms         Misc Flux comms session operations\n"
"   comms-stats   Display comms message counters, etc.\n"
"   zio           Manipulate KVS streams\n"
);
}

int setup_lua_env (const char *exedir)
{
    char *s;

    /* XXX: For now Lua paths are set relative to path of the executable.
     *  Once we know where these things will be installed we can make these
     *  paths configurable on installation.
     */
    s = getenv ("LUA_CPATH");
    setenvf ("LUA_CPATH", 1, "%s/dlua/?.so;%s", exedir, s ? s : ";;");
    s = getenv ("LUA_PATH");
    setenvf ("LUA_PATH", 1, "%s/dlua/?.lua;%s", exedir, s ? s : ";;");
    return (0);
}

int main (int argc, char *argv[])
{
    int ch;
    bool hopt = false;
    bool vopt = false;
    char *flux_exe_dir = dir_self ();

    log_init ("flux");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'T': /* --tmpdir PATH */
                if (setenv ("FLUX_TMPDIR", optarg, 1) < 0)
                    err_exit ("setenv FLUX_TMPDIR=%s", optarg);
                break;
            case 't': /* --trace-apisock */
                if (setenv ("FLUX_TRACE_APISOCK", "1", 1) < 0)
                    err_exit ("setenv FLUX_TRACE_APISOCK=1");
                break;
            case 'M': /* --module-path */
                if (setenv ("FLUX_MODULE_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_MODULE_PATH=%s", optarg);
                break;
            case 'x': /* --exec-path */
                if (setenv ("FLUX_EXEC_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_EXEC_PATH=%s", optarg);
                break;
            case 'B': /* --cmbd-path */
                if (setenv ("FLUX_CMBD_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_CMBD_PATH=%s", optarg);
                break;
            case 'v': /* --verbose */
                vopt = true;
                break;
            case 'h': /* --help  */
                hopt = true;
                break;
            default:
                usage ();
                exit (1);
        }
    }
    argc -= optind;
    argv += optind;

    /* We are executing 'flux' from a path that is not the installed path.
     * Presume we are in $top_builddir/src/cmd and set up environment
     * accordingly.
     */
    if (strcmp (flux_exe_dir, X_BINDIR) != 0) {
        glob_t gl;
        char *modpath;
        if (setenv ("FLUX_EXEC_PATH", ".", 0) < 0)
            err_exit ("setenv");
        if (glob ("../modules/*/.libs", GLOB_ONLYDIR, NULL, &gl) == 0) {
            modpath = argv_concat (gl.gl_pathc, gl.gl_pathv, ":");
            globfree (&gl);
            if (setenv ("FLUX_MODULE_PATH", modpath, 0) < 0)
                err_exit ("setenv");
            free (modpath);
        }
        if (setenv ("FLUX_CMBD_PATH", "../broker/cmbd", 0) < 0)
            err_exit ("setenv");
    } else {
        if (setenv ("FLUX_EXEC_PATH", EXEC_PATH, 0) < 0)
            err_exit ("setenv");
        if (setenv ("FLUX_CMBD_PATH", CMBD_PATH, 0) < 0)
            err_exit ("setenv");
    }

    if (hopt) {
        if (argc > 0) {
            char *av[] = { argv[0], "--help", NULL };
            exec_subcommand (vopt, av);
        } else
            help ();
        exit (0);
    }
    if (argc == 0) {
        usage ();
        exit (1);
    }
    if (vopt)
        dump_environment ();
    exec_subcommand (vopt, argv);

    free (flux_exe_dir);

    log_fini ();

    return 0;
}

void dump_environment_one (const char *name)
{
    char *s = getenv (name);
    printf ("%20s=%s\n", name, s ? s : "<unset>");
}

void dump_environment (void)
{
    dump_environment_one ("FLUX_EXEC_PATH");
    dump_environment_one ("FLUX_MODULE_PATH");
    dump_environment_one ("FLUX_CMBD_PATH");
    dump_environment_one ("FLUX_TMPDIR");
    dump_environment_one ("FLUX_TRACE_APISOCK");
}

/*  Return directory containing this executable.  Caller must free.
 *   (using non-portable /proc/self/exe support for now)
 */
char *dir_self (void)
{
    char  flux_exe_path [MAXPATHLEN];
    char *flux_exe_dir;

    memset (flux_exe_path, 0, MAXPATHLEN);
    if (readlink ("/proc/self/exe", flux_exe_path, MAXPATHLEN - 1) < 0)
        err_exit ("readlink (/proc/self/exe)");
    flux_exe_dir = dirname (flux_exe_path);
    return xstrdup (flux_exe_dir);
}

void exec_subcommand_dir (bool vopt, const char *dir,char *argv[],
                          const char *prefix)
{
    char *path;
    if (asprintf (&path, "%s/%s%s", dir, prefix ? prefix : "", argv[0]) < 0)
        oom ();
    if (vopt)
        msg ("trying to exec %s", path);
    execvp (path, argv); /* no return if successful */
    free (path);
}

void exec_subcommand (bool vopt, char *argv[])
{
    char *searchpath = getenv ("FLUX_EXEC_PATH"); // assert != NULL
    char *cpy = xstrdup (searchpath);
    char *dir, *saveptr = NULL, *a1 = cpy;

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        exec_subcommand_dir (vopt, dir, argv, "flux-");
        //exec_subcommand_dir (vopt, dir, argv, NULL); /* deprecated */
        a1 = NULL;
    }
    free (cpy);
    msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
