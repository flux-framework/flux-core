/* flux-rmmod.c - remove module subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-rmmod [--rank N] modulename [modulename ...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int i;
    int rank = -1;

    log_init ("flux-rmmod");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank N */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    for (i = optind; i < argc; i++) {
        if (flux_rmmod (h, rank, argv[i]) < 0) {
            if (errno == ESRCH)
                msg ("module `%s' is not loaded", argv[i]);
            else
                err ("flux_rmmod `%s' failed", argv[i]);
        } else
            msg ("module `%s' successfully unloaded", argv[i]);
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
