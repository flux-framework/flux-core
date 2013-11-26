/* flux-info.c - flux info subcommand */

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
"Usage: flux-info\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int rank, size;
    bool treeroot;

    log_init ("flux-info");

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
    if (optind != argc)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (flux_info (h, &rank, &size, &treeroot) < 0)
        err_exit ("flux_info");
    printf ("rank=%d\n", rank);
    printf ("size=%d\n", size);
    printf ("treeroot=%s\n", treeroot ? "true" : "false");

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
