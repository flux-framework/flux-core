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

#include "src/common/libutil/blobref.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libcontent/content.h"

static void load_to_fd (flux_t *h, int fd, const char *blobref, int flags)
{
    flux_future_t *f;
    const uint8_t *data;
    int size;

    if (!(f = content_load_byblobref (h, blobref, flags))
        || content_load_get (f, (const void **)&data, &size) < 0)
        log_msg_exit ("error loading blob: %s", future_strerror (f, errno));
    if (write_all (fd, data, size) < 0)
        log_err_exit ("write");
    flux_future_destroy (f);
}

static void store_blob (flux_t *h, const void *data, size_t size, int flags)
{
    flux_future_t *f;
    const char *blobref;

    if (!(f = content_store (h, data, size, flags))
        || content_store_get_blobref (f, &blobref) < 0)
        log_msg_exit ("error storing blob: %s", future_strerror (f, errno));
    printf ("%s\n", blobref);
    flux_future_destroy (f);
}

static void store_from_fd (flux_t *h, int fd, size_t chunksize, int flags)
{
    void *data;
    ssize_t total_size;

    if ((total_size = read_all (fd, &data)) < 0)
        log_err_exit ("read from stdin");
    if (chunksize == 0)
        chunksize = total_size;
    if (total_size == 0) // an empty blob is still valid
        store_blob (h, data, total_size, flags);
    else {
        for (off_t offset = 0; offset < total_size; offset += chunksize) {
            if (chunksize > total_size - offset)
                chunksize = total_size - offset;
            store_blob (h, data + offset, chunksize, flags);
        }
    }
    free (data);
}

static int internal_content_load (optparse_t *p, int ac, char *av[])
{
    int n;
    flux_t *h;
    int flags = 0;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;

    n = optparse_option_index (p);
    if (n == ac) {
        char blobref[BLOBREF_MAX_STRING_SIZE];
        int count = 0;
        while ((fgets (blobref, sizeof (blobref), stdin))) {
            int len = strlen (blobref);
            if (blobref[len - 1] == '\n')
                blobref[len - 1] = '\0';
            load_to_fd (h, STDOUT_FILENO, blobref, flags);
            count++;
        }
        if (count == 0)
            log_msg_exit ("no blobrefs were specified");
    }
    else while (n < ac) {
        load_to_fd (h, STDOUT_FILENO, av[n++], flags);
    }
    flux_close (h);
    return (0);
}

static int internal_content_store (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    int flags = 0;
    int chunksize = optparse_get_int (p, "chunksize", 0);

    if (optparse_option_index (p) != ac || chunksize < 0) {
        optparse_print_usage (p);
        exit (1);
    }
    if (optparse_hasopt (p, "bypass-cache"))
        flags |= CONTENT_FLAG_CACHE_BYPASS;
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    store_from_fd (h, STDIN_FILENO, chunksize, flags);
    flux_close (h);
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
    { .name = "chunksize", .has_arg = 1, .arginfo = "N",
      .usage = "Limit blob size to N bytes with 0=unlimited (default 0)", },
      OPTPARSE_TABLE_END
};

static struct optparse_subcommand content_subcmds[] = {
    { "load",
      "[OPTIONS] BLOBREF ...",
      "Concatenate blobs stored under BLOBREF(s) to stdout",
      internal_content_load,
      0,
      load_opts,
    },
    { "store",
      "[OPTIONS]",
      "Store blob from stdin, print BLOBREF(s) on stdout",
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
