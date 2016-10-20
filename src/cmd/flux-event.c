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
#include <libgen.h>
#include <argz.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libjson-c/json.h"


static int event_pub (optparse_t *p, int argc, char **argv);
static int event_sub (optparse_t *p, int argc, char **argv);

static void event_pub_register (optparse_t *p);
static void event_sub_register (optparse_t *p);

int main (int argc, char *argv[])
{
    int n;
    optparse_t *p;
    flux_t *h;

    log_init ("flux-event");
    if (!(p = optparse_create ("flux-event")))
        log_err_exit ("optparse_create");

    event_pub_register (p);
    event_sub_register (p);

    n = optparse_parse_args (p, argc, argv);
    if (n == argc || n <= 0) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    optparse_set_data (p, "handle", h);
    if (optparse_run_subcommand (p, argc, argv) < 0)
        log_err_exit ("subcommand");

    flux_close (h);
    optparse_destroy (p);
    log_fini ();
    return 0;
}

static void event_pub_register (optparse_t *parent)
{
    optparse_t *p = optparse_add_subcommand (parent, "pub", event_pub);
    if (p == NULL)
        log_err_exit ("optparse_add_subcommand");
    if (optparse_set (p, OPTPARSE_USAGE, "topic [json]") != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_set (USAGE)");
}

static int event_pub (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    char *topic = argv[1];  /* "pub" should be argv0 */
    flux_msg_t *msg = NULL;
    char *json_str = NULL;
    int e, n;

    if (!topic) {
        optparse_print_usage (p);
        exit (1);
    }

    if (!(h = optparse_get_data (p, "handle")))
        log_err_exit ("failed to get handle");

    n = optparse_optind (p);
    if (n < argc - 1) {
        size_t len = 0;
        json_object *o;
        if ((e = argz_create (argv + n + 1, &json_str, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (json_str, len, ' ');
        if (*json_str != '{' || !(o = json_tokener_parse (json_str)))
            log_msg_exit ("JSON argument must be a valid JSON object");
        json_object_put (o);
    }
    if (!(msg = flux_event_encode (topic, json_str))
              || flux_send (h, msg, 0) < 0)
        log_err_exit ("sending event");
    if (json_str)
        free (json_str);
    flux_msg_destroy (msg);
    return (0);
}

static void subscribe_all (flux_t *h, int tc, char **tv)
{
    int i;
    for (i = 0; i < tc; i++) {
        if (flux_event_subscribe (h, tv[i]) < 0)
            log_err_exit ("flux_event_subscribe");
    }
}

static void unsubscribe_all (flux_t *h, int tc, char **tv)
{
    int i;
    for (i = 0; i < tc; i++) {
        if (flux_event_unsubscribe (h, tv[i]) < 0)
            log_err_exit ("flux_event_unsubscribe");
    }
}

void event_sub_register (optparse_t *op)
{
    optparse_err_t e;
    optparse_t *p;
    struct optparse_option opts [] = {
        {
         .name = "count", .key = 'c', .group = 1,
         .has_arg = 1, .arginfo = "N",
         .usage = "Process N events then exit" },
        {
         .name = "raw", .key = 'r', .group = 1,
         .has_arg = 0, .arginfo = NULL,
         .usage = "Dump raw event message" },
        OPTPARSE_TABLE_END
    };

    if (!(p = optparse_add_subcommand (op, "sub", event_sub)))
        log_err_exit ("optparse_add_subcommand");

    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_err_exit ("event sub: optparse_add_option_table");

    e = optparse_set (p, OPTPARSE_USAGE, "[options] [topic...]");
    if (e != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_set (USAGE) failed");
}

static int event_sub (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_msg_t *msg;
    int n, count;
    bool raw = false;

    if (!(h = optparse_get_data (p, "handle")))
        log_err_exit ("failed to get handle");

    /* Since output is line-based with undeterministic amount of time
     * between lines, force stdout to be line buffered so our output
     * is immediately available in stream, even if stdout is not a tty.
     */
    setlinebuf (stdout);

    n = optparse_optind (p);
    if (n < argc)
        subscribe_all (h, argc - n, argv + n);
    else if (flux_event_subscribe (h, "") < 0)
        log_err_exit ("flux_event_subscribe");

    n = 0;
    count = optparse_get_int (p, "count", 0);
    raw = optparse_hasopt (p, "raw");
    while ((msg = flux_recv (h, FLUX_MATCH_EVENT, 0))) {
        if (raw) {
            flux_msg_fprint (stdout, msg);
        } else {
            const char *topic;
            const char *json_str;
            if (flux_msg_get_topic (msg, &topic) < 0
                    || flux_msg_get_json (msg, &json_str) < 0) {
                printf ("malformed message ignored\n");
            } else {
                printf ("%s\t%s\n", topic, json_str ? json_str : "");
            }
        }
        flux_msg_destroy (msg);

        /* Wait for at most count events */
        if (count > 0 && ++n == count)
            break;
    }
    /* FIXME: add SIGINT handler to exit above loop and clean up.
     */
    if (argc > 1)
        unsubscribe_all (h, argc - 1, argv + 1);
    else if (flux_event_unsubscribe (h, "") < 0)
        log_err_exit ("flux_event_subscribe");
    return (0);
}



/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
