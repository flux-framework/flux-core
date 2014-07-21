/* flux-stats.c - flux stats subcommand */

#define _GNU_SOURCE
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "cmb.h"
#include "util.h"
#include "log.h"

#define OPTIONS "hcCp:s:t:r"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"clear",      no_argument,        0, 'c'},
    {"clear-all",  no_argument,        0, 'C'},
    {"rusage",     no_argument,        0, 'r'},
    {"parse",      required_argument,  0, 'p'},
    {"scale",      required_argument,  0, 's'},
    {"type",       required_argument,  0, 't'},
    { 0, 0, 0, 0 },
};

static void parse_json (const char *n, json_object *o, double scale,
                         json_type type);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-stats [--scale N] [--type int|double] --parse a[.b]... [node!]name\n"
"       flux-stats --clear-all name\n"
"       flux-stats --clear [node!]name\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch, rank = -1;
    char *p, *target, *rankstr = NULL;
    char *objname = NULL;
    bool copt = false;
    bool Copt = false;
    bool ropt = false;
    double scale = 1.0;
    json_type type = json_type_object;
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
            case 'r': /* --rusage */
                ropt = true;
                break;
            case 'p': /* --parse objname */
                objname = optarg;
                break;
            case 's': /* --scale N */
                scale = strtod (optarg, NULL);
                break;
            case 't': /* --type TYPE */
                if (!strcasecmp (optarg, "int"))
                    type = json_type_int;
                else if (!strcasecmp (optarg, "double"))
                    type = json_type_double;
                else
                    usage ();
                break;
            case 'd': /* --parse-double objname */
                objname = optarg;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    if (scale != 1.0 && type != json_type_int && type != json_type_double)
        msg_exit ("Use --scale only with --type int or --type double");
    target = argv[optind];
    if ((p = strchr (target, '!'))) {
        rankstr = target;
        *p++ = '\0';
        rank = strtoul (rankstr, NULL, 10);
        target = p;
    }

    if (Copt && rankstr)
        msg_exit ("Use --clear not --clear-all to clear a single node.");

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (copt) {
        if ((response = flux_rank_rpc (h, rank, NULL, "%s.stats.clear",target)))
            errn_exit (EPROTO, "unexpected response to %s.clearstats", target);
        if (errno != 0)
            err_exit ("flux_rank_rpc %s.clearstats", target);
    } else if (Copt) {
        if (flux_event_send (h, NULL, "%s.stats.clear", target) < 0)
            err_exit ("flux_event_send %s.stats.clear", target);
    } else if (ropt) {
        if (!(response = flux_rank_rpc (h, rank, NULL, "%s.rusage", target)))
            errn_exit (EPROTO, "flux_rank_rpc %s.rusage", target);
        parse_json (objname, response, scale, type);
        json_object_put (response);
    } else {
        if (!(response = flux_rank_rpc (h, rank, NULL, "%s.stats.get", target)))
            err_exit ("flux_rank_rpc %s.stats", target);
        parse_json (objname, response, scale, type);
        json_object_put (response);
    }
    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

static void parse_json (const char *n, json_object *o, double scale,
                         json_type type)
{
    if (n) {
        char *cpy = xstrdup (n);
        char *name, *saveptr = NULL, *a1 = cpy;

        while ((name = strtok_r (a1, ".", &saveptr))) {
            if (!(o = json_object_object_get (o, name)))
                err_exit ("`%s' not found in response", n);
            a1 = NULL;
        }
        free (cpy);
    }
    switch (type) {
        case json_type_double: {
            double d;
            errno = 0;
            d = json_object_get_double (o);
            if (errno != 0)
                err_exit ("couldn't convert value to double");
            printf ("%lf\n", d * scale);
            break;
        } 
        case json_type_int: {
            double d;
            errno = 0;
            d = json_object_get_double (o);
            if (errno != 0)
                err_exit ("couldn't convert value to double (en route to int)");
            printf ("%d\n", (int)(d * scale));
            break;
        }
        default: {
            const char *s;
            s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PRETTY);
            printf ("%s\n", s);
            break;
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
