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

static struct optparse_option pub_opts[] = {
    { .name = "raw", .key = 'r', .has_arg = 0,
      .usage = "Interpret event payload as raw.",
    },
    OPTPARSE_TABLE_END
};

static void event_pub_register (optparse_t *parent)
{
    if (optparse_reg_subcommand (parent, "pub", event_pub,
                                 "[OPTIONS] topic [payload]", NULL, 0,
                                 pub_opts) != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_reg_subcommand");
}

static int event_pub (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "handle");
    int optindex = optparse_option_index (p);
    const char *topic;
    char *payload = NULL;
    flux_msg_t *msg;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    topic = argv[optindex++];

    /* Concatenate any remaining arguments to form payload.
     */
    if (optindex < argc) {
        size_t len = 0;
        int e;
        if ((e = argz_create (argv + optindex, &payload, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (payload, len, ' ');
    }

    if (optparse_hasopt (p, "raw")) {
        int payloadsz = payload ? strlen (payload) : 0;
        if (!(msg = flux_event_encode_raw (topic, payload, payloadsz)))
            log_err_exit ("flux_event_encode_raw");
    }
    else {
        if (!(msg = flux_event_encode (topic, payload)))
            log_err_exit ("flux_event_encode");
    }
    if (flux_send (h, msg, 0) < 0)
        log_err_exit ("flux_send");

    flux_msg_destroy (msg);
    free (payload);
    return (0);
}

static int subscribe_multiple (flux_t *h, int tc, char **tv)
{
    int i;

    if (tc == 0)
        return flux_event_subscribe (h, "");
    for (i = 0; i < tc; i++) {
        if (flux_event_subscribe (h, tv[i]) < 0)
            return -1;
    }
    return 0;
}

static int unsubscribe_multiple (flux_t *h, int tc, char **tv)
{
    int i;
    if (tc == 0)
        return flux_event_unsubscribe (h, "");
    for (i = 0; i < tc; i++) {
        if (flux_event_unsubscribe (h, tv[i]) < 0)
            return -1;
    }
    return 0;
}

static const struct optparse_option sub_opts [] = {
    { .name = "count", .key = 'c', .has_arg = 1, .arginfo = "N", .group = 1,
      .usage = "Process N events then exit"
    },
    OPTPARSE_TABLE_END
};

void event_sub_register (optparse_t *parent)
{
    if (optparse_reg_subcommand (parent, "sub", event_sub,
                                 "[OPTIONS] [topic...]", NULL, 0,
                                 sub_opts) != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_reg_subcommand");
}

static int event_sub (optparse_t *p, int argc, char **argv)
{
    flux_t *h = optparse_get_data (p, "handle");
    int optindex = optparse_option_index (p);
    flux_msg_t *msg;
    int count, n;

    if (!h)
        log_err_exit ("failed to get handle");

    /* Since output is line-based with undeterministic amount of time
     * between lines, force stdout to be line buffered so our output
     * is immediately available in stream, even if stdout is not a tty.
     */
    setlinebuf (stdout);

    if (subscribe_multiple (h, argc - optindex, argv + optindex) < 0)
        log_err_exit ("flux_event_subscribe");

    n = 0;
    count = optparse_get_int (p, "count", 0);
    while ((msg = flux_recv (h, FLUX_MATCH_EVENT, 0))) {
        const char *topic;
        const char *json_str;
        if (flux_event_decode (msg, &topic, &json_str) < 0)
            printf ("malformed message ignored\n");
        else
            printf ("%s\t%s\n", topic, json_str ? json_str : "");
        flux_msg_destroy (msg);

        /* Wait for at most count events */
        if (count > 0 && ++n == count)
            break;
    }
    /* FIXME: add SIGINT handler to exit above loop and clean up.
     */
    if (unsubscribe_multiple (h, argc - optindex, argv + optindex) , 0)
        log_err_exit ("flux_event_subscribe");
    return (0);
}



/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
