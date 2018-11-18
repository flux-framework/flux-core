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
#include <stdbool.h>
#include <argz.h>
#include <flux/core.h>
#include <inttypes.h>

#include "src/common/libutil/log.h"


#define OPTIONS "+hr:"
static const struct option longopts[] = {
    {"help",       no_argument,        0, 'h'},
    {"rank",       required_argument,  0, 'r'},
    { 0, 0, 0, 0 },
};

void usage (void)
{
    fprintf (stderr,
"Usage: flux-comms [-r N] idle\n"
"       flux-comms        info\n"
"       flux-comms [-r N] panic [msg ...]\n"
);
    exit (1);
}

static char *flux_lspeer (flux_t *h, int rank)
{
    int saved_errno;
    uint32_t nodeid = (rank == -1 ? FLUX_NODEID_ANY : rank);
    flux_future_t *f;
    const char *json_str;
    char *ret = NULL;

    if (!(f = flux_rpc (h, "cmb.lspeer", NULL, nodeid, 0)))
        goto done;
    if (flux_rpc_get (f, &json_str) < 0)
        goto done;
    if (!json_str) {
        errno = EPROTO;
        goto done;
    }
    ret = strdup (json_str);
done:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return ret;
}

int main (int argc, char *argv[])
{
    flux_t *h;
    int ch;
    uint32_t rank = FLUX_NODEID_ANY; /* local */
    char *cmd;
    int e;

    log_init ("flux-comms");

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
                usage ();
                break;
            case 'r': /* --rank NODESET */
                rank = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
                break;
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];
    if (rank != FLUX_NODEID_ANY
            && (!strcmp (cmd, "recover-all") || !strcmp (cmd, "info")))
        usage ();

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!strcmp (cmd, "idle")) {
        if (optind != argc)
            usage ();
        char *peers;
        if (!(peers = flux_lspeer (h, rank)))
            log_err_exit ("flux_lspeer");
        printf ("%s\n", peers);
        free (peers);
    } else if (!strcmp (cmd, "panic")) {
        char *reason = NULL;
        int flags = 0;
        size_t len = 0;
        if (optind < argc) {
            if ((e = argz_create (argv + optind, &reason, &len)) != 0)
                log_errn_exit (e, "argz_create");
            argz_stringify (reason, len, ' ');
        }
        if (flux_panic (h, rank, flags,
                        reason ? reason : "user request") < 0)
            log_err_exit ("flux_panic");
        free (reason);
    } else if (!strcmp (cmd, "info")) {
        int arity;
        uint32_t rank, size;
        const char *s;
        if (flux_get_rank (h, &rank) < 0 || flux_get_size (h, &size) < 0)
            log_err_exit ("flux_get_rank/size");
        if (!(s = flux_attr_get (h, "tbon.arity")))
            log_err_exit ("flux_attr_get tbon.arity");
        arity = strtoul (s, NULL, 10);
        printf ("rank=%"PRIu32"\n", rank);
        printf ("size=%"PRIu32"\n", size);
        printf ("arity=%d\n", arity);
    } else
        usage ();

    flux_close (h);
    log_fini ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
