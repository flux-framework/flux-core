/* flux.c - Flux command front-end */

#define _GNU_SOURCE
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
#include <json/json.h>
#include <sys/param.h>

#include "cmb.h"
#include "log.h"
#include "util.h"
#include "zmsg.h"

void exec_subcommand (const char *exec_path, char *argv[]);

#define OPTIONS "+s:tx:h"
static const struct option longopts[] = {
    {"socket-path",     required_argument,  0, 's'},
    {"trace-apisocket", no_argument,        0, 't'},
    {"exec-path",       required_argument,  0, 'x'},
    {"help",            no_argument,        0, 'h'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr, 
"Usage: flux [--socket-path PATH] [--exec-path PATH]\n"
"            [--trace-apisock] [--help] COMMAND ARGS\n"
);
}

static void help (void)
{
    usage ();
    fprintf (stderr, "\nThe most commonly used flux commands are:\n"
"   kvs        Get and put simple values in the Flux key-value store\n"
"   kvswatch   Watch values in the Flux key-value store\n"
"   ping       Time round-trip RPC to a Flux plugin\n"
"   mecho      Time round-trip group RPC to the mecho plugin\n"
"   route      Manipulate routing tables in the Flux message broker\n"
"   stats      Obtain message counts from a Flux plugin\n"
"   barrier    Execute a Flux barrier\n"
"   snoop      Snoop on local Flux message broker traffic\n"
"   event      Send and receive Flux events\n"
);
}

int main (int argc, char *argv[])
{
    int ch;
    char *exec_path;
    bool hopt = false;
    
    log_init ("flux-kvs");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 's': /* --socket-path PATH */
                if (setenv ("FLUX_API_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_API_PATH=%s", optarg);
                break;
            case 't': /* --trace-apisock */
                if (setenv ("FLUX_TRACE_APISOCK", "1", 1) < 0)
                    err_exit ("setenv FLUX_TRACE_APISOCK=1");
                break;
            case 'x': /* --exec-path */
                if (setenv ("FLUX_EXEC_PATH", optarg, 1) < 0)
                    err_exit ("setenv FLUX_EXEC_PATH=%s", optarg);
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

    if (!(exec_path = getenv ("FLUX_EXEC_PATH"))) {
        exec_path = EXEC_PATH;
        if (setenv ("FLUX_EXEC_PATH", exec_path, 1) < 0)
            err_exit ("setenv FLUX_EXEC_PATH=%s", optarg);
    }
    if (hopt) {
        if (argc > 0) {
            char *av[] = { argv[0], "--help", NULL };
            exec_subcommand (exec_path, av);
        } else
            help ();
        exit (0);
    }
    if (argc == 0) {
        usage ();
        exit (1);
    }

    exec_subcommand (exec_path, argv);

    log_fini ();

    return 0;
}

void exec_subcommand (const char *searchpath, char *argv[])
{
    char *cpy = xstrdup (searchpath);
    char *path, *dir, *saveptr, *a1 = cpy;

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        if (asprintf (&path, "%s/%s", dir, argv[0]) < 0)
            oom ();
        execvp (path, argv); /* no return if successful */
        free (path);
        a1 = NULL;
    }
    free (cpy);
    msg_exit ("`%s' is not a flux command.  See 'flux --help'", argv[0]);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
