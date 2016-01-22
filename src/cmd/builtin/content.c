/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include "builtin.h"

#include "src/common/libutil/sha1.h"
#include "src/common/libutil/shastring.h"
#include "src/common/libutil/readall.h"

static int internal_content_load (optparse_t *p, int ac, char *av[])
{
    int n;
    const char *blobref;
    uint8_t *data;
    int size;
    flux_t h;
    flux_conf_t cf;
    flux_rpc_t *rpc;
    const char *topic;

    if (!(cf = optparse_get_data (p, "conf")))
        msg_exit ("optparse_get ('conf') failed");

    n = optparse_optind (p);
    if (n != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    blobref = av[n];
    if (!(h = builtin_get_flux_handle (p)))
        err_exit ("flux_open");
    if (optparse_hasopt (p, "bypass-cache"))
        topic = "content-backing.load";
    else
        topic = "content.load";
    if (!(rpc = flux_rpc_raw (h, topic, blobref, strlen (blobref) + 1, 0, 0)))
        err_exit ("%s", topic);
    if (flux_rpc_get_raw (rpc, NULL, &data, &size) < 0)
        err_exit ("%s", topic);
    if (write_all (STDOUT_FILENO, data, size) < 0)
        err_exit ("write");
    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}

static int internal_content_store (optparse_t *p, int ac, char *av[])
{
    const uint32_t blob_size_limit = 1048576; /* RFC 10 */
    uint8_t *data;
    int size;
    flux_t h;
    flux_conf_t cf;
    flux_rpc_t *rpc;
    const char *topic;

    if (!(cf = optparse_get_data (p, "conf")))
        msg_exit ("optparse_get ('conf') failed");

    if (optparse_optind (p)  != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if ((size = read_all (STDIN_FILENO, &data)) < 0)
        err_exit ("read");
    if (!(h = builtin_get_flux_handle (p)))
        err_exit ("flux_open");
    if (optparse_hasopt (p, "dry-run")) {
        int flags;
        const char *hashfun;

        if (size > blob_size_limit)
            errn_exit (EFBIG, "content-store");
        if (!(hashfun = flux_attr_get (h, "content-hash", &flags)))
            err_exit ("flux_attr_get content-hash");
        if (!strcmp (hashfun, "sha1")) {
            uint8_t hash[SHA1_DIGEST_SIZE];
            char hashstr[SHA1_STRING_SIZE];
            SHA1_CTX sha1_ctx;

            SHA1_Init (&sha1_ctx);
            SHA1_Update (&sha1_ctx, (uint8_t *)data, size);
            SHA1_Final (&sha1_ctx, hash);
            sha1_hashtostr (hash, hashstr);
            printf ("%s\n", hashstr);
        } else
            msg_exit ("content-store: unsupported hash function: %s", hashfun);
    } else {
        const char *blobref;
        int blobref_size;
        if (optparse_hasopt (p, "bypass-cache"))
            topic = "content-backing.store";
        else
            topic = "content.store";
        if (!(rpc = flux_rpc_raw (h, topic, data, size, 0, 0)))
            err_exit ("%s", topic);
        if (flux_rpc_get_raw (rpc, NULL, &blobref, &blobref_size) < 0)
            err_exit ("%s", topic);
        if (!blobref || blobref[blobref_size - 1] != '\0')
            msg_exit ("%s: protocol error", topic);
        printf ("%s\n", blobref);
        flux_rpc_destroy (rpc);
    }
    flux_close (h);
    free (data);
    return (0);
}

static int internal_content_flush (optparse_t *p, int ac, char *av[])
{
    flux_t h;
    flux_conf_t cf;
    flux_rpc_t *rpc = NULL;
    const char *topic = "content.flush";

    if (!(cf = optparse_get_data (p, "conf")))
        msg_exit ("optparse_get ('conf') failed");

    if (optparse_optind (p)  != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        err_exit ("flux_open");
    if (!(rpc = flux_rpc (h, topic, NULL, FLUX_NODEID_ANY, 0)))
        err_exit ("%s", topic);
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        err_exit ("%s", topic);
    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}

static int internal_content_dropcache (optparse_t *p, int ac, char *av[])
{
    flux_t h;
    flux_conf_t cf;
    flux_rpc_t *rpc = NULL;
    const char *topic = "content.dropcache";

    if (!(cf = optparse_get_data (p, "conf")))
        msg_exit ("optparse_get ('conf') failed");

    if (optparse_optind (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        err_exit ("flux_open");
    if (!(rpc = flux_rpc (h, topic, NULL, FLUX_NODEID_ANY, 0)))
        err_exit ("%s", topic);
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        err_exit ("%s", topic);
    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}

int cmd_content (optparse_t *p, int ac, char *av[])
{
    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option load_opts[] = {
    { .name = "bypass-cache",  .key = 'b',  .has_arg = 0,
      .usage = "Load directly from rank 0 content service", },
    OPTPARSE_TABLE_END,
};

static struct optparse_option store_opts[] = {
    { .name = "bypass-cache",  .key = 'b',  .has_arg = 0,
      .usage = "Store directly to rank 0 content service", },
    { .name = "dry-run",  .key = 'd',  .has_arg = 0,
      .usage = "Compute SHA1 but don't actually store value" },
      OPTPARSE_TABLE_END,
};

static struct optparse_subcommand content_subcmds[] = {
    { "load",
      "[OPTIONS] BLOBREF",
      "Load blob for digest BLOBREF to stdout",
      internal_content_load,
      load_opts,
    },
    { "store",
      "[OPTIONS]",
      "Store blob from stdin, print BLOBREF on stdout",
      internal_content_store,
      store_opts,
    },
     { "dropcache",
       NULL,
       "Drop non-essential entries from local content cache",
       internal_content_dropcache,
       NULL,
    },
     { "flush",
       NULL,
       "Flush dirty entries from local content cache",
       internal_content_flush,
       NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_content_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "content", cmd_content, NULL, "Access content store", NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "content"),
                                  content_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
