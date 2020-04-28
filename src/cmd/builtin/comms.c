/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "builtin.h"

#include <inttypes.h>
#include <argz.h>
#include <assert.h>

#include "src/common/libidset/idset.h"

static int internal_comms_info (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    uint32_t rank, size;
    const char *arity;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        log_err_exit ("flux_get_rank");
    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");
    if (!(arity = flux_attr_get (h, "tbon.arity")))
        log_err_exit ("flux_attr_get tbon.arity");
    printf ("rank=%"PRIu32"\n", rank);
    printf ("size=%"PRIu32"\n", size);
    printf ("arity=%s\n", arity);

    flux_close (h);
    return 0;
}

static int internal_comms_panic (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;
    flux_t *h;

    if (optindex < ac) {
        e = argz_create (av + optindex, &argz, &argz_len);
        if (e != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (argz, argz_len, ' ');
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (flux_panic (h, FLUX_NODEID_ANY, 0, argz ? argz : "user request") < 0)
        log_err_exit ("flux_panic");
    free (argz);

    flux_close (h);
    return 0;
}

static int internal_comms_lspeer (optparse_t *p, int ac, char *av[])
{
    flux_t *h;
    flux_future_t *f;
    const char *peers;

    if (optparse_option_index (p) != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (!(f = flux_rpc (h, "overlay.lspeer", NULL, FLUX_NODEID_ANY, 0)))
        log_err_exit ("flux_rpc");
    if (flux_rpc_get (f, &peers) < 0)
        log_msg_exit ("overlay.lspeer: %s", flux_future_error_string (f));
    printf ("%s\n", peers);
    flux_future_destroy (f);

    flux_close (h);
    return 0;
}

/* Return true if all members of 'idset1' are in 'idset2'.
 */
static bool is_subset_of (struct idset *idset1, struct idset *idset2)
{
    unsigned int id = idset_first (idset1); // IDSET_INVALID_ID if idset1=NULL
    while (id != IDSET_INVALID_ID) {
        if (!idset_test (idset2, id))       // idset_test()=false if idset2=NULL
            return false;
        id = idset_next (idset1, id);
    }
    return true;
}

/* Parse idset argument named 'name'.
 * If its value is "all" then return an idset containing 0...size-1.
 */
struct idset *parse_idset_arg (optparse_t *p, const char *name, uint32_t size)
{
    const char *arg;
    struct idset *idset;

    if ((arg = optparse_get_str (p, name, NULL)) == NULL)
        return NULL;

    if (!strcmp (arg, "all")) {
        if (!(idset = idset_create (size, 0)))
            log_err_exit ("error creating 'all' idset");
        if (idset_range_set (idset, 0, size - 1) < 0)
            log_err_exit ("error populating 'all' idset");
    }
    else {
        if (!(idset = idset_decode (arg)))
            log_msg_exit ("%s argument cannot be parsed", name);
        if (idset_last (idset) >= size)
            log_msg_exit ("%s argument range error (size=%u)", name, size);
    }
    return idset;
}

static int internal_comms_up (optparse_t *p, int ac, char *av[])
{
    int optindex = optparse_option_index (p);
    flux_t *h;
    uint32_t size;
    flux_future_t *f;
    int flags = 0;
    const char *s;
    struct idset *target = NULL;
    struct idset *cur;
    bool done;

    if (optindex != ac) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");

    if ((target = parse_idset_arg (p, "wait-for", size)))
        flags |= FLUX_RPC_STREAMING;

    if (!(f = flux_rpc (h, "hello.idset", NULL, 0, flags)))
        log_err_exit ("flux_rpc");
    do {
        if (flux_rpc_get_unpack (f, "{s:s}", "idset", &s) < 0)
            log_msg_exit ("hello.idset: %s", flux_future_error_string (f));
        if (!optparse_hasopt (p, "quiet"))
            printf ("%s\n", s);
        /* If target is non-NULL, then we keep listening until the
         * returned idset includes all the ranks in target.
         */
        if (target) {
            if (!(cur = idset_decode (s)))
                log_msg_exit ("hello.idset: bad idset in response");
            done = is_subset_of (target, cur);
            idset_destroy (cur);
        }
        else
            done = true;
        flux_future_reset (f);
    } while (!done);
    flux_future_destroy (f);

    idset_destroy (target);
    flux_close (h);
    return 0;
}

int cmd_comms (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-comms");

    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option up_opts[] = {
    { .name = "wait-for",
      .key = 'w',
      .has_arg = 1,
      .arginfo = "IDSET",
      .usage = "Monitor idset changes until IDSET ranks are up",
    },
    { .name = "quiet",
      .key = 'q',
      .has_arg = 0,
      .usage = "Suppress printing idset",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_subcommand comms_subcmds[] = {
    { "lspeer",
      "",
      "List broker peers with idle times",
      internal_comms_lspeer,
      0,
      NULL,
    },
    { "info",
      "",
      "List rank, size, TBON branching factor",
      internal_comms_info,
      0,
      NULL,
    },
    { "panic",
      "[msg ...]",
      "Tell broker to print message and call _exit(1)",
      internal_comms_panic,
      0,
      NULL,
    },
    { "up",
      "[OPTIONS]",
      "List available broker ranks",
      internal_comms_up,
      0,
      up_opts,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_comms_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "comms", cmd_comms, NULL, "Manage broker communications", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "comms"),
                                  comms_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
