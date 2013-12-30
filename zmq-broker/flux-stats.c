/* flux-stats.c - flux stats subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hcCp:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"clear",      no_argument,        0, 'c'},
    {"clear-all",  no_argument,        0, 'C'},
    {"parse",      required_argument,  0, 'p'},
    { 0, 0, 0, 0 },
};

static void parse_json (const char *n, json_object *o);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-stats [--clear] [--parse obj[.obj...]] [node!]name\n"
"       flux-stats --clear-all name\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *target;
    char *objname = NULL;
    bool copt = false;
    bool Copt = false;
    json_object *response;

    log_init ("flux-stats");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'c': /* --clear */
                copt = true;
                break;
            case 'C': /* --clear-all */
                Copt = true;
                break;
            case 'p': /* --parse objname */
                objname = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    target = argv[optind];
    if (Copt && strchr (target, '!'))
        msg_exit ("Use --clear not --clear-all to clear a single node.");

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (copt) {
        if (!(response = flux_rpc (h, NULL, "%s.stats.clear", target))) {
            if (errno != 0)
                err_exit ("flux_rpc %s.clearstats", target);
        } else
            json_object_put (response);
    } else if (Copt) {
        if (flux_event_send (h, NULL, "event.%s.stats.clear", target) < 0)
            err_exit ("flux_event_send event.%s.stats.clear", target);
    } else {
        if (!(response = flux_rpc (h, NULL, "%s.stats.get", target)))
            err_exit ("flux_rpc %s.stats", target);
        parse_json (objname, response);
        json_object_put (response);
    }
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void parse_json (const char *n, json_object *o)
{
    if (n) {
        char *cpy = xstrdup (n);
        char *name, *saveptr, *a1 = cpy;

        while ((name = strtok_r (a1, ".", &saveptr))) {
            if (!(o = json_object_object_get (o, name)))
                err_exit ("`%s' not found in response", n);
            a1 = NULL;
        }
    }
    printf ("%s\n",
            json_object_to_json_string_ext (o, JSON_C_TO_STRING_PRETTY));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
