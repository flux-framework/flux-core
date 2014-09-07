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

/* flux-event.c - flux event subcommand */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <getopt.h>
#include <json/json.h>
#include <assert.h>
#include <libgen.h>

#include "flux.h"
#include "zmsg.h"
#include "log.h"

#define OPTIONS "hp:s"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"publish",    required_argument,  0, 'p'},
    {"subscribe",  no_argument,        0, 's'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-event --pub message [json]\n"
"       flux-event --sub [topic]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *arg = NULL;
    char *pub = NULL;
    bool sub = false;

    log_init ("flux-event");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --publish message [json] */
                pub = optarg;
                break;
            case 's': /* --subscribe [topic] */
                sub = true;
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc && optind != argc - 1)
        usage ();
    if (optind == argc - 1)
        arg = argv[optind];
    if (!pub && !sub)
        usage ();

    if (!(h = cmb_init ()))
        err_exit ("cmb_init");

    if (pub) {
        json_object *o = NULL;
        enum json_tokener_error e;
        if (arg && !(o = json_tokener_parse_verbose (arg, &e)))
            msg_exit ("json parse error: %s", json_tokener_error_desc (e));
        if (flux_event_send (h, o, "%s", pub) < 0 )
            err_exit ("flux_event_send");
        if (o)
            json_object_put (o);
    }
    if (sub) {
        json_object *o = NULL;
        const char *s;
        char *tag = NULL;
        if (flux_event_subscribe (h, arg) < 0)
            err_exit ("flux_event_subscribe");
        while (flux_event_recv (h, &o, &tag, false) == 0) {
            printf ("--------------------------------------\n");
            if (tag) {
                printf ("%s\n", tag);
                free (tag);
                tag = NULL;
            } else
                printf ("<empty tag>\n"); 
            if (o) {
                s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
                printf ("%s\n", s);
                json_object_put (o);
                o = NULL;
            } else
                printf ("<empty paylod>\n");
        }
        if (flux_event_unsubscribe (h, arg) < 0)
            err_exit ("flux_event_unsubscribe");
    }

    flux_handle_destroy (&h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
