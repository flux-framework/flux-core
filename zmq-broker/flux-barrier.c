/* flux-barrier.c - flux barrier subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hn:t:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"nprocs",     required_argument,  0, 'n'},
    {"test-iterations", required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-barrier [--nprocs N] [--test-iterations N] name\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    struct timespec t0;
    char *name, *tname;
    int nprocs = 1;
    int iter = 1;
    int i;

    log_init ("flux-barrier");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'n': /* --nprocs N */
                nprocs = strtoul (optarg, NULL, 10);
                break;
            case 't': /* --test-iterations N */
                iter = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    name = argv[optind];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    for (i = 0; i < iter; i++) {
        monotime (&t0);
        asprintf (&tname, "%s.%d", name, i);
        if (flux_barrier (h, tname, nprocs) < 0)
            err_exit ("flux_barrier");
        printf ("barrier name=%s nprocs=%d time=%0.3f ms\n",
             tname, nprocs, monotime_since (t0));
        free (tname);
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
