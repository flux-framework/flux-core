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
#include <json.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"


static void event_pub (flux_t h, int argc, char **argv);
static void event_sub (flux_t h, int argc, char **argv);

#define OPTIONS "h"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-event pub topic [json]\n"
"       flux-event sub [topic...]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t h;
    int ch;
    char *cmd;

    log_init ("flux-event");

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
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    if (!strcmp (cmd, "pub"))
        event_pub (h, argc - optind, argv + optind);
    else if (!strcmp (cmd, "sub"))
        event_sub (h, argc - optind, argv + optind);
    else
        usage ();

    flux_close (h);
    log_fini ();
    return 0;
}

static void event_pub (flux_t h, int argc, char **argv)
{
    char *topic = argv[0];
    json_object *o = NULL;

    if (argc > 1) {
        enum json_tokener_error e;
        char *s = NULL;
        size_t len = 0;
        if (argz_create (argv + 1, &s, &len) < 0)
            oom ();
        argz_stringify (s, len, ' ');
        if (!(o = json_tokener_parse_verbose (s, &e)))
            msg_exit ("json parse error: %s", json_tokener_error_desc (e));
        free (s);
    }
    if (flux_event_send (h, o, "%s", topic) < 0 )
        err_exit ("flux_event_send");
    if (o)
        json_object_put (o);
}

static void subscribe_all (flux_t h, int tc, char **tv)
{
    int i;
    for (i = 0; i < tc; i++) {
        if (flux_event_subscribe (h, tv[i]) < 0)
            err_exit ("flux_event_subscribe");
    }
}

static void unsubscribe_all (flux_t h, int tc, char **tv)
{
    int i;
    for (i = 0; i < tc; i++) {
        if (flux_event_unsubscribe (h, tv[i]) < 0)
            err_exit ("flux_event_unsubscribe");
    }
}

static void event_sub (flux_t h, int argc, char **argv)
{
    json_object *o = NULL;
    const char *s;
    char *topic = NULL;

    if (argc > 0)
        subscribe_all (h, argc, argv);
    else if (flux_event_subscribe (h, "") < 0)
        err_exit ("flux_event_subscribe");

    while (flux_event_recv (h, &o, &topic, false) == 0) {
        if (topic) {
            printf ("%s\t", topic);
            free (topic);
            topic = NULL;
        } else
            printf ("\t\t"); 
        if (o) {
            s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
            printf ("%s\n", s);
            json_object_put (o);
            o = NULL;
        } else
            printf ("\n");
    }
    /* FIXME: add SIGINT handler to exit above loop and clean up.
     */
    if (argc > 0)
        unsubscribe_all (h, argc, argv);
    else if (flux_event_unsubscribe (h, "") < 0)
        err_exit ("flux_event_subscribe");
}



/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
