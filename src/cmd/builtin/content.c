/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include "builtin.h"

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <jansson.h>

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libcontent/content.h"

static int internal_content_load (optparse_t *p, int ac, char *av[])
{
    int n;
    const char *ref;
    const uint8_t *data;
    int size;
    flux_t *h;
    flux_future_t *f;
    int flags = 0;

    n = optparse_option_index (p);
    if (n != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    ref = av[n];
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;
    if (!(f = content_load_byblobref (h, ref, flags)))
        log_err_exit ("content_load_byblobref");
    if (content_load_get (f, (const void **)&data, &size) < 0)
        log_err_exit ("content_load_get");
    if (write_all (STDOUT_FILENO, data, size) < 0)
        log_err_exit ("write");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_content_store (optparse_t *p, int ac, char *av[])
{
    uint8_t *data;
    int size;
    flux_t *h;
    flux_future_t *f;
    const char *blobref;
    int flags = 0;

    if (optparse_option_index (p)  != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if ((size = read_all (STDIN_FILENO, (void **)&data)) < 0)
        log_err_exit ("read");
    if (!(f = content_store (h, data, size, flags)))
        log_err_exit ("content_store");
    if (content_store_get_blobref (f, &blobref) < 0)
        log_err_exit ("content_store_get_blobref");
    printf ("%s\n", blobref);
    flux_future_destroy (f);
    flux_close (h);
    free (data);
    return (0);
}

static int internal_content_flush (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f = NULL;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "content.flush", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("content.flush");
    if (flux_rpc_get (f, NULL) < 0)
        log_err_exit ("content.flush");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_content_dropcache (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f = NULL;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "content.dropcache", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("content.dropcache");
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("content.dropcache");
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static int internal_content_mmap (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    int blobsize = 1048576;
    flux_t *h;
    flux_future_t *f;
    char path[PATH_MAX];
    size_t index;
    json_t *blobrefs;
    json_t *blob;

    if (optindex == ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < ac) {
        if (!realpath (av[optindex++], path))
            log_err_exit ("path");
    }
    if (optindex < ac) {
        errno = 0;
        blobsize = strtoul (av[optindex++], NULL, 10);
        if (errno > 0)
            log_msg_exit ("error parsing optional blobsize argument");
    }
    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h,
                             "content.mmap",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i}",
                             "path", path,
                             "blobsize", blobsize))
        || flux_rpc_get_unpack (f, "{s:o}", "blobrefs", &blobrefs) < 0)
        log_err_exit ("content.mmap: %s", future_strerror (f, errno));
    json_array_foreach (blobrefs, index, blob) {
        printf ("%s\n", json_string_value (blob));
    }
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

static int internal_content_munmap (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    flux_future_t *f;
    char path[PATH_MAX];

    if (optindex == ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optindex < ac) {
        if (!realpath (av[optindex++], path))
            log_err_exit ("path");
    }
    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc_pack (h,
                             "content.munmap",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "path", path))
        || flux_rpc_get (f, NULL) < 0)
        log_err_exit ("content.munmap: %s", future_strerror (f, errno));
    flux_future_destroy (f);
    flux_close (h);
    return 0;
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
      0,
      load_opts,
    },
    { "store",
      "[OPTIONS]",
      "Store blob from stdin, print BLOBREF on stdout",
      internal_content_store,
      0,
      store_opts,
    },
    { "dropcache",
      NULL,
      "Drop non-essential entries from local content cache",
      internal_content_dropcache,
      0,
      NULL,
    },
    { "flush",
      NULL,
      "Flush dirty entries from local content cache",
      internal_content_flush,
      0,
      NULL,
    },
    { "mmap",
      "PATH [BLOBSIZE]",
      "Map a file into the content cache",
      internal_content_mmap,
      0,
      NULL,
    },
    { "munmap",
      "PATH",
      "Unmap a file from the content cache",
      internal_content_munmap,
      0,
      NULL,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_content_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "content", cmd_content, NULL, "Access content store", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "content"),
                                  content_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
