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
#include <inttypes.h>
#include <unistd.h>
#include <flux/core.h>
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"


#define OPTIONS "+hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"ranks",      required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

static void usage (int code)
{
    fprintf (stderr,
"Usage: flux-hwloc reload [OPTIONS] DIR\n"
"where OPTIONS are:\n"
"       -h,--help          print this message.\n"
"       -r,--ranks=NODESET send the hwloc reload request to brokers\n"
"                          in NODESET. NODESET is a string containing a \n"
"                          bracketed set of ranks or \"all\" as a shorthand\n"
"                          for all ranks in the session. Examples of NODESET\n"
"                          strings are \"[0-255]\" and \"[1-2,5]\". If not given,\n"
"                          NODESET is set to all.\n\n"
"where DIR must contain one xml file per rank prefixed with the rank number \n"
"(e.g., 0.xml, 1.xml, etc).\n"
);
    exit (code);
}

static int handle_hwloc_reload (flux_t h, const char *nodeset, const char *path)
{
    int rc = 0;
    char *key = NULL;
    char *val = NULL;
    uint32_t size = 0;
    uint32_t rank = 0;
    flux_rpc_t *rpc = NULL;

    if ((rc = flux_get_size (h, &size)) != 0) {
        flux_log (h, LOG_ERR, "error in flux_get_size.");
        goto done;
    }

    for (rank = 0; rank < size; rank++) {
        key = xasprintf ("config.resource.hwloc.xml.%"PRIu32, rank);
        val = xasprintf ("%s/%"PRIu32".xml", path, rank);
        if ((rc = kvs_put_string (h, key, val) != 0)) {
            flux_log (h, LOG_ERR, "flux_kvs_put: %s", strerror (errno));
            goto done;
        }
        free (key);
        free (val);
    }
    if ((rc = kvs_commit (h)) != 0) {
        fprintf (stderr, "flux_kvs_commit: %s\n", strerror (errno));
        goto done;
    }

    if (!(rpc = flux_rpc_multi (h, "resource-hwloc.reload", NULL, nodeset, 0))) {
        fprintf (stderr, "flux_rpc_multi: %s\n", strerror (errno));
        rc = -1;
        goto done;
    }
    while (!flux_rpc_completed (rpc)) {
        const char *json_str = NULL;
        uint32_t nodeid;
        if ((rc += flux_rpc_get (rpc, &nodeid, &json_str) < 0))
            flux_log (h, LOG_ERR, "rpc(%"PRIu32"): %s", nodeid, strerror (errno));
    }
    flux_rpc_destroy (rpc);

done:
    return rc;
}

/******************************************************************************
 *                                                                            *
 *                            Main entry point                                *
 *                                                                            *
 ******************************************************************************/

int main (int argc, char *argv[])
{
    flux_t h;
    int rc = 0;
    int ch = 0;
    char *cmd = NULL;
    char *nodeset = NULL;

    log_init ("flux-hwloc");
    if (argc < 2)
        usage (1);
    cmd = argv[1];
    argc--;
    argv++;

    log_init ("flux-hwloc");
    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage (1);
                break;
            case 'r': /* --rankset=NODESET */
                nodeset = xstrdup (optarg);
                break;
            default:
                usage (1);
                break;
        }
    }

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (!nodeset)
        nodeset = xasprintf ("all");

    if (!strcmp ("reload", cmd)) {
        char path[PATH_MAX] = {'\0'};
        char **a = argv + optind;
        if (!(*a))
            usage (1);
        if ( realpath (*a, path) != NULL ) {
            rc = handle_hwloc_reload (h, (const char *)nodeset, (const char *)path);
        } else {
            rc = -1;
            fprintf (stderr,
                     "error resolving the path (%s): %s\n", *a, strerror (errno));
        }
    }
    else
        usage (1);

    flux_close (h);
    log_fini ();

    if (nodeset)
        free (nodeset);

    return (!rc)? 0: 1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
