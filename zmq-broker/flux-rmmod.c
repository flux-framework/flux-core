/* flux-rmmod.c - flux rmmod subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-rmmod modulename\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;

    log_init ("flux-rmmod");

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
    if (optind != argc - 1)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (flux_rmmod (h, argv[optind]) < 0)
        err_exit ("flux_rmmod");

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
