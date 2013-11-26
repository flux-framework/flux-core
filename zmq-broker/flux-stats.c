/* flux-stats.c - flux stats subcommand */

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
"Usage: flux-stats [node!]tag\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *target;
    json_object *response;

    log_init ("flux-stats");

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
    target = argv[optind];

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (!(response = flux_rpc (h, NULL, "%s.stats", target)))
        err_exit ("flux_rpc %s.stats", optarg);
    printf ("%s\n", json_object_to_json_string_ext (response,
            JSON_C_TO_STRING_PRETTY));
    json_object_put (response);

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
