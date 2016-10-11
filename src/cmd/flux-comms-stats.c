/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <getopt.h>
#include <libgen.h>
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libjson-c/json.h"


#define OPTIONS "hcCp:s:t:r:R"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"clear",      no_argument,        0, 'c'},
    {"clear-all",  no_argument,        0, 'C'},
    {"rusage",     no_argument,        0, 'R'},
    {"parse",      required_argument,  0, 'p'},
    {"scale",      required_argument,  0, 's'},
    {"type",       required_argument,  0, 't'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

static void parse_json (const char *n, const char *json_str, double scale,
                        json_type type);

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-comms-stats [--scale N] [--type int|double] --parse a[.b]... name\n"
"       flux-comms-stats --clear-all name\n"
"       flux-comms-stats --clear name\n"
"       flux-comms-stats --rusage name\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    uint32_t nodeid = FLUX_NODEID_ANY;
    char *target;
    char *objname = NULL;
    bool copt = false;
    bool Copt = false;
    bool Ropt = false;
    double scale = 1.0;
    json_type type = json_type_object;

    log_init ("flux-stats");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            case 'c': /* --clear */
                copt = true;
                break;
            case 'C': /* --clear-all */
                Copt = true;
                break;
            case 'R': /* --rusage */
                Ropt = true;
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
        log_msg_exit ("Use --scale only with --type int or --type double");
    target = argv[optind++];

    if (Copt && nodeid != FLUX_NODEID_ANY)
        log_msg_exit ("Use --clear not --clear-all to clear a single node.");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (copt) {
        flux_rpc_t *rpc;
        char *topic = xasprintf ("%s.stats.clear", target);
        if (!(rpc = flux_rpc (h, topic, NULL, nodeid, 0))
                                || flux_rpc_get (rpc, NULL) < 0)
            log_err_exit ("%s", topic);
        free (topic);
        flux_rpc_destroy (rpc);
    } else if (Copt) {
        char *topic = xasprintf ("%s.stats.clear", target);
        flux_msg_t *msg = flux_event_encode (target, NULL);
        if (!msg || flux_send (h, msg, 0) < 0)
            log_err_exit ("sending event");
        flux_msg_destroy (msg);
        free (topic);
    } else if (Ropt) {
        flux_rpc_t *rpc;
        const char *json_str;
        char *topic = xasprintf ("%s.rusage", target);
        if (!(rpc = flux_rpc (h, topic, NULL, nodeid, 0))
                                || flux_rpc_get (rpc, &json_str) < 0)
            log_err_exit ("%s", topic);
        parse_json (objname, json_str, scale, type);
        free (topic);
        flux_rpc_destroy (rpc);
    } else {
        flux_rpc_t *rpc;
        const char *json_str;
        char *topic = xasprintf ("%s.stats.get", target);
        if (!(rpc = flux_rpc (h, topic, NULL, nodeid, 0))
                                || flux_rpc_get (rpc, &json_str) < 0)
            log_err_exit ("%s", topic);
        parse_json (objname, json_str, scale, type);
        free (topic);
        flux_rpc_destroy (rpc);
    }
    flux_close (h);
    log_fini ();
    return 0;
}

static void parse_json (const char *n, const char *json_str, double scale,
                        json_type type)
{
    json_object *o = json_tokener_parse (json_str);
    if (!o)
        log_err_exit ("error parsing JSON response");
    if (n) {
        char *cpy = xstrdup (n);
        char *name, *saveptr = NULL, *a1 = cpy;

        while ((name = strtok_r (a1, ".", &saveptr))) {
            if (!json_object_object_get_ex (o, name, &o) || o == NULL)
                log_err_exit ("`%s' not found in response", n);
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
                log_err_exit ("couldn't convert value to double");
            printf ("%lf\n", d * scale);
            break;
        }
        case json_type_int: {
            double d;
            errno = 0;
            d = json_object_get_double (o);
            if (errno != 0)
                log_err_exit ("couldn't convert value to double (en route to int)");
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
    json_object_put (o);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
