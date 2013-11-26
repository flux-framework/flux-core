/* flux-snoop.c - flux snoop subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "zmsg.h"
#include "log.h"

#define OPTIONS "h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-snoop [subscription]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *topic = NULL;
    zmsg_t *zmsg;

    log_init ("flux-snoop");

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
    if (optind != argc && optind != argc - 1)
        usage ();
    if (optind == argc - 1)
        topic = argv[optind];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (flux_snoop_subscribe (h, topic) < 0)
        err_exit ("flux_snoop_subscribe");
    while ((zmsg = flux_snoop_recvmsg (h, false))) {
        zmsg_dump_compact (zmsg);
        zmsg_destroy (&zmsg);
    }
    if (flux_event_unsubscribe (h, topic) < 0)
        err_exit ("flux_snoop_unsubscribe");

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
