/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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

#include <stdlib.h>
#include <time.h>
#include <flux/idset.h>

#include "src/common/libpmi/simple_client.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

static struct pmi_simple_client *client;
static char kvsname[1024];

static void client_init (void)
{
    int result;
    const char *vars[] = { "PMI_FD", "PMI_RANK", "PMI_SIZE" }; // required

    for (int i = 0; i < ARRAY_SIZE (vars); i++) {
        if (!getenv (vars[i]))
            log_msg_exit ("%s is missing from the environment", vars[i]);
    }
    client = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                          getenv ("PMI_RANK"),
                                          getenv ("PMI_SIZE"),
                                          getenv ("PMI_SPAWNED"));
    if (!client)
        log_err_exit ("could not create PMI client");

    result = pmi_simple_client_init (client);
    if (result != PMI_SUCCESS)
        log_msg_exit ("pmi init failed: %s", pmi_strerror (result));

    result = pmi_simple_client_kvs_get_my_name (client,
                                                kvsname,
                                                sizeof (kvsname));
    if (result != PMI_SUCCESS)
        log_msg_exit ("could not fetch kvsname: %s", pmi_strerror (result));
}

static void client_finalize (void)
{
    int result;

    result = pmi_simple_client_finalize (client);
    if (result != PMI_SUCCESS)
        log_msg_exit ("pmi finalize failed: %s", pmi_strerror (result));
    pmi_simple_client_destroy (client);
}

static void client_barrier (void)
{
    int result;

    result = pmi_simple_client_barrier (client);
    if (result != PMI_SUCCESS)
        log_msg_exit ("pmi barrier failed: %s", pmi_strerror (result));
}

static void client_kvs_get (const char *key, char *buf, int size)
{
    int result;

    result = pmi_simple_client_kvs_get (client,
                                        kvsname,
                                        key,
                                        buf,
                                        size);
    if (result != PMI_SUCCESS)
        log_msg_exit ("could not fetch %s: %s", key, pmi_strerror (result));
}

static int internal_cmd_get (optparse_t *p, int argc, char *argv[])
{
    int n = optparse_option_index (p);
    const char *arg = optparse_get_str (p, "ranks", "0");
    struct idset *ranks = NULL;

    if (!streq (arg, "all")) {
        if (!(ranks = idset_decode (arg)))
            log_msg_exit ("could not decode --ranks argument");
    }
    client_init ();
    if (!ranks || idset_test (ranks, client->rank)) {
        while (n < argc) {
            char val[1024];
            client_kvs_get (argv[n++], val, sizeof (val));
            printf ("%s\n", val);
        }
    }
    client_finalize ();
    idset_destroy (ranks);

    return 0;
}

static int internal_cmd_barrier (optparse_t *p, int argc, char *argv[])
{
    int n = optparse_option_index (p);
    int count = optparse_get_int (p, "count", 1);
    struct timespec t;
    const char *label;

    if (n != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(label = getenv ("FLUX_JOB_CC")))
        if (!(label = getenv ("FLUX_JOB_ID")))
            label = "0";
    client_init ();
    client_barrier (); // don't let task launch stragglers skew timing
    while (count-- > 0) {
        monotime (&t);
        client_barrier ();
        if (client->rank == 0) {
            printf ("%s: completed pmi barrier on %d tasks in %0.3fs.\n",
                    label,
                    client->size,
                    monotime_since (t) / 1000);
            fflush (stdout);
        }
    }
    client_finalize ();

    return 0;
}

static int cmd_pmi (optparse_t *p, int argc, char *argv[])
{
    log_init ("flux-pmi");

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        exit (1);

    return 0;
}

static struct optparse_option barrier_opts[] = {
    { .name = "count",      .has_arg = 1, .arginfo = "N",
       .usage = "Execute N barrier operations (default 1)", },
    OPTPARSE_TABLE_END,
};
static struct optparse_option get_opts[] = {
    { .name = "ranks",      .has_arg = 1, .arginfo = "{IDSET|all}",
       .usage = "Print value on specified ranks (default: 0)", },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand pmi_subcmds[] = {
    { "barrier",
      "[OPTIONS]",
      "Execute PMI barrier",
      internal_cmd_barrier,
      0,
      barrier_opts,
    },
    { "get",
      "[OPTIONS]",
      "Get PMI KVS key",
      internal_cmd_get,
      0,
      get_opts,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_pmi_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
            "pmi", cmd_pmi, NULL, "Simple PMI test client", 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "pmi"),
                                  pmi_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
