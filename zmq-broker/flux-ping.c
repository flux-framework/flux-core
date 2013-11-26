/* flux-ping.c - flux ping subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hp:d:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"pad-bytes",  required_argument,  0, 'p'},
    {"delay-msec", required_argument,  0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-ping [--pad-bytes N] [--delay-msec N] [node!]tag\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    int msec = 1000;
    int bytes = 0;
    char *pad = NULL;
    int seq;
    struct timespec t0;
    char *route, *target;

    log_init ("flux-ping");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pad-bytes N */
                bytes = strtoul (optarg, NULL, 10);
                pad = xzmalloc (bytes + 1);
                memset (pad, 'p', bytes);
                break;
            case 'd': /* --delay-msec N */
                msec = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    target = argv[optind];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    for (seq = 0; ; seq++) {
        monotime (&t0);
        if (!(route = flux_ping (h, target, pad, seq)))
            err_exit ("%s.ping", target);
        printf ("%s.ping pad=%d seq=%d time=%0.3f ms (%s)\n",
                target, bytes, seq, monotime_since (t0), route);
        free (route);
        usleep (msec * 1000);
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
