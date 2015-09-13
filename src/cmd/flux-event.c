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
#include "src/common/libutil/optparse.h"


static void event_pub (flux_t h, int argc, char **argv);
static void event_sub (flux_t h, int argc, char **argv);

#define OPTIONS "+h"
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
    cmd = argv[optind];

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
    char *topic = argv[1];  /* "pub" should be argv0 */
    flux_msg_t *msg = NULL;
    char *json_str = NULL;

    if (argc > 2) {
        size_t len = 0;
        if (argz_create (argv + 2, &json_str, &len) < 0)
            oom ();
        argz_stringify (json_str, len, ' ');
    }
    if (!(msg = flux_event_encode (topic, json_str))
              || flux_send (h, msg, 0) < 0)
        err_exit ("sending event");
    if (json_str)
        free (json_str);
    flux_msg_destroy (msg);
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

static optparse_t event_sub_get_options (int *argcp, char ***argvp)
{
    int n, e;
    optparse_t p;
    struct optparse_option opts [] = {
        {
         .name = "count", .key = 'c', .group = 1,
         .has_arg = 1, .arginfo = "N",
         .usage = "Process N events then exit" },
        OPTPARSE_TABLE_END
    };

    if (!(p = optparse_create ("flux-event sub")))
        err_exit ("event sub: optparse_create");

    if ((e = optparse_add_option_table (p, opts)))
        err_exit ("event sub: optparse_add_option_table");

    optparse_set (p, OPTPARSE_USAGE, "[OPTIONS] [topic...]");
    if ((n = optparse_parse_args (p, *argcp, *argvp)) < 0)
	exit (1);

    /* Adjust argc,argv past option fields
     */
    (*argcp) -= n;
    (*argvp) += n;

    return (p);
}

static void event_sub (flux_t h, int argc, char **argv)
{
    flux_msg_t *msg;
    int n, count;
    optparse_t p = event_sub_get_options (&argc, &argv);


    /* Since output is line-based with undeterministic amount of time
     * between lines, force stdout to be line buffered so our output
     * is immediately available in stream, even if stdout is not a tty.
     */
    setlinebuf (stdout);

    if (argc > 0)
        subscribe_all (h, argc, argv);
    else if (flux_event_subscribe (h, "") < 0)
        err_exit ("flux_event_subscribe");

    n = 0;
    count = optparse_get_int (p, "count", 0);
    while ((msg = flux_recv (h, FLUX_MATCH_EVENT, 0))) {
        const char *topic;
        const char *json_str;
        if (flux_msg_get_topic (msg, &topic) < 0
                || flux_msg_get_payload_json (msg, &json_str) < 0) {
            printf ("malformed message ignored\n");
        } else {
            printf ("%s\t%s\n", topic, json_str ? json_str : "");
        }
        flux_msg_destroy (msg);

        /* Wait for at most count events */
        if (count > 0 && ++n == count)
            break;
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
