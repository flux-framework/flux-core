/* flux-route.c - flux ping subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hqa:d:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"query",      no_argument,        0, 'q'},
    {"add",        required_argument,  0, 'a'},
    {"delete",     required_argument,  0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-route --query\n"
"       flux-route [--add|--del] dst:gw\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *gw = NULL;
    char *dst = NULL;
    bool qopt = false;
    bool aopt = false;
    bool dopt = false;
    json_object *o;

    log_init ("flux-route");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'q': /* --query */
                qopt = true;
                break;
            case 'a': /* --add dst:gw */
                dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                aopt = true;
                break;
            case 'd': /* --del dst:gw */
                dst = xstrdup (optarg);
                if (!(gw = strchr (dst, ':')))
                    usage ();
                *gw++ = '\0';
                dopt = true;
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

    if (aopt) {
        if (flux_route_add (h, dst, gw) < 0)
            err ("flux_route_add %s via %s", dst, gw);
    } else if (dopt) {
        if (flux_route_del (h, dst, gw) < 0)
            err ("flux_route_del %s via %s", dst, gw);
    } else if (qopt) {
        if (!(o = flux_route_query (h)))
            err_exit ("flux_route_query");
        printf ("%s\n", json_object_to_json_string_ext (o,
                                    JSON_C_TO_STRING_PRETTY));
        json_object_put (o);
    } else
        usage ();

    flux_handle_destroy (&h);
    log_fini ();
    if (gw)
        free (gw);
    if (dst)
        free (dst);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
