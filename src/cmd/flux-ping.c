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
#include <unistd.h>
#include <string.h>
#include <json.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"


#define OPTIONS "hp:d:r:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    {"pad-bytes",  required_argument,  0, 'p'},
    {"delay-msec", required_argument,  0, 'd'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr, 
"Usage: flux-ping [--rank N] [--pad-bytes N] [--delay-msec N] target\n"
);
    exit (1);
}

static char *ping (flux_t h, uint32_t nodeid, const char *name, const char *pad,
                   int seq);

int main (int argc, char *argv[])
{
    flux_t h;
    int ch, seq, bytes = 0, msec = 1000;
    uint32_t nodeid = FLUX_NODEID_ANY;
    char *rankstr = NULL, *route, *target, *pad = NULL;
    struct timespec t0;

    log_init ("flux-ping");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'p': /* --pad-bytes N */
                bytes = strtoul (optarg, NULL, 10);
                pad = xzmalloc (bytes + 1);
                memset (pad, 'p', bytes);
                break;
            case 'd': /* --delay-msec N */
                msec = strtoul (optarg, NULL, 10);
                break;
            case 'r': /* --rank N */
                nodeid = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind != argc - 1)
        usage ();
    target = argv[optind++];

    if (nodeid == FLUX_NODEID_ANY) {
        char *endptr;
        uint32_t n = strtoul (target, &endptr, 10);
        if (endptr != target)
            nodeid = n;
        if (*endptr == '!')
            target = endptr + 1;
        else
            target = endptr;
    }
    if (*target == '\0')
        target = "cmb";
    if (nodeid != FLUX_NODEID_ANY)
        rankstr = xasprintf ("%u", nodeid);

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");

    for (seq = 0; ; seq++) {
        monotime (&t0);
        if (!(route = ping (h, nodeid, target, pad, seq)))
            err_exit ("%s%s%s.ping", rankstr ? rankstr : "",
                                     rankstr ? "!" : "",
                                     target);
        printf ("%s%s%s.ping pad=%d seq=%d time=%0.3f ms (%s)\n",
                rankstr ? rankstr : "",
                rankstr ? "!" : "",
                target, bytes, seq, monotime_since (t0), route);
        free (route);
        usleep (msec * 1000);
    }

    flux_close (h);
    log_fini ();
    return 0;
}

/* Ping plugin 'name'.
 * 'pad' is a string used to increase the size of the ping packet for
 * measuring RTT in comparison to rough message size.
 * 'seq' is a sequence number.
 * The pad and seq are echoed in the response, and any mismatch will result
 * in an error return with errno = EPROTO.
 * A string representation of the route taken is the return value on success
 * (caller must free).  On error, return NULL with errno set.
 */
static char *ping (flux_t h, uint32_t nodeid, const char *name, const char *pad,
                   int seq)
{
    json_object *request = util_json_object_new_object ();
    json_object *response = NULL;
    int rseq;
    const char *route, *rpad;
    char *ret = NULL;
    char *topic = xasprintf ("%s.ping", name);

    if (pad)
        util_json_object_add_string (request, "pad", pad);
    util_json_object_add_int (request, "seq", seq);

    if (flux_json_rpc (h, nodeid, topic, request, &response) < 0)
        goto done;
    if (util_json_object_get_int (response, "seq", &rseq) < 0
            || util_json_object_get_string (response, "route", &route) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (seq != rseq) {
        msg ("%s: seq not echoed back", __FUNCTION__);
        errno = EPROTO;
        goto done;
    }
    if (pad) {
        if (util_json_object_get_string (response, "pad", &rpad) < 0
                                || !rpad || strlen (pad) != strlen (rpad)) {
            msg ("%s: pad not echoed back", __FUNCTION__);
            errno = EPROTO;
            goto done;
        }
    }
    ret = strdup (route);
done:
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    free (topic);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
