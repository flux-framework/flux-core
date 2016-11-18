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

#include <unistd.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/readall.h"

static int internal_content_load (optparse_t *p, int ac, char *av[])
{
    int n;
    const char *ref;
    uint8_t *data;
    int size;
    flux_t *h;
    flux_rpc_t *r;
    int flags = 0;

    n = optparse_optind (p);
    if (n != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    ref = av[n];
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;
    if (!(r = flux_content_load (h, ref, flags)))
        log_err_exit ("flux_content_load");
    if (flux_content_load_get (r, &data, &size) < 0)
        log_err_exit ("flux_content_load_get");
    if (write_all (STDOUT_FILENO, data, size) < 0)
        log_err_exit ("write");
    flux_rpc_destroy (r);
    flux_close (h);
    return (0);
}

static int internal_content_store (optparse_t *p, int ac, char *av[])
{
    uint8_t *data;
    int size;
    flux_t *h;
    flux_rpc_t *r;
    const char *blobref;
    int flags = 0;

    if (optparse_optind (p)  != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if ((size = read_all (STDIN_FILENO, &data)) < 0)
        log_err_exit ("read");
    if (!(r = flux_content_store (h, data, size, flags)))
        log_err_exit ("flux_content_store");
    if (flux_content_store_get (r, &blobref) < 0)
        log_err_exit ("flux_content_store_get");
    printf ("%s\n", blobref);
    flux_rpc_destroy (r);
    flux_close (h);
    free (data);
    return (0);
}

static int internal_content_flush (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_rpc_t *rpc = NULL;

    if (optparse_optind (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(rpc = flux_rpc (h, "content.flush", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("content.flush");
    if (flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("content.flush");
    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}

static int internal_content_dropcache (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_rpc_t *rpc = NULL;

    if (optparse_optind (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(rpc = flux_rpc (h, "content.dropcache", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("content.dropcache");
    if (flux_rpc_get (rpc, NULL) < 0)
        log_err_exit ("content.dropcache");
    flux_rpc_destroy (rpc);
    flux_close (h);
    return (0);
}

static int spam_max_inflight;
static int spam_cur_inflight;

static void store_completion (flux_rpc_t *r, void *arg)
{
    flux_t *h = arg;
    const char *blobref;

    if (flux_content_store_get (r, &blobref) < 0)
        log_err_exit ("store");
    printf ("%s\n", blobref);
    flux_rpc_destroy (r);
    if (--spam_cur_inflight < spam_max_inflight/2)
        flux_reactor_stop (flux_get_reactor (h));
}

static int internal_content_spam (optparse_t *p, int ac, char *av[])
{
    int i, count;
    flux_rpc_t *r;
    flux_t *h;
    char data[256];
    int size = 256;

    if (ac != 2 && ac != 3) {
        optparse_print_usage (p);
        exit (1);
    }
    count = strtoul (av[1], NULL, 10);
    if (ac == 3)
        spam_max_inflight = strtoul (av[2], NULL, 10);
    else
        spam_max_inflight = 1;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    spam_cur_inflight = 0;
    i = 0;
    while (i < count || spam_cur_inflight > 0) {
        while (i < count && spam_cur_inflight < spam_max_inflight) {
            snprintf (data, size, "spam-o-matic pid=%d seq=%d", getpid(), i);
            if (!(r = flux_content_store (h, data, size, 0)))
                log_err_exit ("flux_content_store(%d)", i);
            if (flux_rpc_then (r, store_completion, h) < 0)
                log_err_exit ("flux_rpc_then(%d)", i);
            spam_cur_inflight++;
            i++;
        }
        if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
            log_err ("flux_reactor_run");
    }
    return (0);
}

int cmd_content (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-content");

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
    { "spam",
      "N [M]",
      "Store N random entries, keeping M requests in flight (default 1)",
      internal_content_spam,
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
