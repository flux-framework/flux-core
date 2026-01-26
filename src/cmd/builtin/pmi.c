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

#include "src/common/libpmi/upmi.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

static struct upmi *upmi;

static int internal_cmd_get (optparse_t *p, int argc, char *argv[])
{
    int n = optparse_option_index (p);
    const char *arg = optparse_get_str (p, "ranks", "0");
    struct idset *ranks = NULL;
    flux_error_t error;
    struct upmi_info info;

    if (!streq (arg, "all")) {
        if (!(ranks = idset_decode (arg)))
            log_msg_exit ("could not decode --ranks argument");
    }
    if (upmi_initialize (upmi, &info, &error) < 0)
        log_msg_exit ("%s", error.text);
    if (!ranks || idset_test (ranks, info.rank)) {
        while (n < argc) {
            const char *key = argv[n++];
            char *val;
            if (upmi_get (upmi, key, -1, &val, &error) < 0)
                log_msg_exit ("get %s: %s", key, error.text);
            printf ("%s\n", val);
            free (val);
        }
    }
    if (upmi_finalize (upmi, &error) < 0)
        log_msg_exit ("finalize: %s", error.text);
    idset_destroy (ranks);

    return 0;
}

static int internal_cmd_barrier (optparse_t *p, int argc, char *argv[])
{
    int n = optparse_option_index (p);
    int count = optparse_get_int (p, "test-count", 1);
    int test_abort = optparse_get_int (p, "test-abort", -1);
    struct timespec t;
    const char *label;
    flux_error_t error;
    struct upmi_info info;

    if (n != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(label = getenv ("FLUX_JOB_CC")))
        if (!(label = getenv ("FLUX_JOB_ID")))
            label = "0";
    if (upmi_initialize (upmi, &info, &error) < 0)
        log_msg_exit ("%s", error.text);

    // don't let task launch stragglers skew timing
    if (upmi_barrier (upmi, &error) < 0)
        log_msg_exit ("barrier: %s", error.text);

    // abort one rank if --test-abort was specified
    if (test_abort != -1) {
        if (info.rank == test_abort) {
            flux_error_t e;
            errprintf (&e, "flux-pmi: rank %d is aborting", info.rank);
            if (upmi_abort (upmi, e.text, &error) < 0) {
                log_msg_exit ("abort: %s", error.text);
            }
        }
    }

    while (count-- > 0) {
        monotime (&t);
        if (upmi_barrier (upmi, &error) < 0)
            log_msg_exit ("barrier: %s", error.text);
        if (info.rank == 0) {
            printf ("%s: completed pmi barrier on %d tasks in %0.3fs.\n",
                    label,
                    info.size,
                    monotime_since (t) / 1000);
            fflush (stdout);
        }
    }

    if (upmi_finalize (upmi, &error) < 0)
        log_msg_exit ("finalize: %s", error.text);

    return 0;
}

static int internal_cmd_exchange (optparse_t *p, int argc, char *argv[])
{
    int n = optparse_option_index (p);
    int count = optparse_get_int (p, "count", 1);
    struct timespec t;
    const char *label;
    flux_error_t error;
    struct upmi_info info;

    if (n != argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (!(label = getenv ("FLUX_JOB_CC")))
        if (!(label = getenv ("FLUX_JOB_ID")))
            label = "0";
    if (upmi_initialize (upmi, &info, &error) < 0)
        log_msg_exit ("%s", error.text);

    // don't let task launch stragglers skew timing
    if (upmi_barrier (upmi, &error) < 0)
        log_msg_exit ("barrier: %s", error.text);

    while (count-- > 0) {
        char key[64];
        char val[64];

        monotime (&t);

        /* Put data from this rank
         */
        snprintf (key, sizeof (key), "key.%d", info.rank);
        snprintf (val,
                  sizeof (val),
                  "%s-%d-%d",
                  info.name,
                  info.rank,
                  info.size);
        if (upmi_put (upmi, key, val, &error) < 0)
            log_msg_exit ("put %s: %s", key, error.text);

        /* Synchronize
         */
        if (upmi_barrier (upmi, &error) < 0)
            log_msg_exit ("barrier: %s", error.text);

        /* Get data from all ranks (and verify).
         */
        for (int rank = 0; rank < info.size; rank++) {
            char *cp;

            snprintf (key, sizeof (key), "key.%d", rank);
            snprintf (val,
                      sizeof (val),
                      "%s-%d-%d",
                      info.name,
                      rank,
                      info.size);
            if (upmi_get (upmi, key, rank, &cp, &error) < 0)
                log_msg_exit ("get %s: %s", key, error.text);
            if (!streq (val, cp))
                log_msg_exit ("get %s: returned unexpected value", key);
            free (cp);
        }

        // timing must reflect completion of gets by all ranks
        if (upmi_barrier (upmi, &error) < 0)
            log_msg_exit ("barrier: %s", error.text);

        if (info.rank == 0) {
            printf ("%s: completed pmi exchange on %d tasks in %0.3fs.\n",
                    label,
                    info.size,
                    monotime_since (t) / 1000);
            fflush (stdout);
        }
    }

    if (upmi_finalize (upmi, &error) < 0)
        log_msg_exit ("finalize: %s", error.text);

    return 0;
}


static void trace (void *arg, const char *text)
{
    fprintf (stderr, "%s\n", text);
}

static int cmd_pmi (optparse_t *p, int argc, char *argv[])
{
    const char *method = optparse_get_str (p, "method", NULL);
    int verbose = optparse_get_int (p, "verbose", 0);
    flux_error_t error;
    int flags = 0;

    log_init ("flux-pmi");

    if (verbose > 0)
        flags |= UPMI_TRACE;
    if (optparse_hasopt (p, "libpmi-noflux"))
        flags |= UPMI_LIBPMI_NOFLUX;
    if (optparse_hasopt (p, "libpmi2-cray"))
        flags |= UPMI_LIBPMI2_CRAY;
    if (!(upmi = upmi_create (method, flags, trace, NULL, &error)))
        log_msg_exit ("%s", error.text);

    if (optparse_run_subcommand (p, argc, argv) != OPTPARSE_SUCCESS)
        exit (1);

    upmi_destroy (upmi);

    return 0;
}

static struct optparse_option barrier_opts[] = {
    { .name = "test-count",      .has_arg = 1, .arginfo = "N",
       .usage = "For testing, execute N barrier operations (default 1)", },
    { .name = "test-abort",      .has_arg = 1, .arginfo = "RANK",
       .usage = "For testing, RANK calls abort instead of barrier", },
    OPTPARSE_TABLE_END,
};
static struct optparse_option get_opts[] = {
    { .name = "ranks",      .has_arg = 1, .arginfo = "{IDSET|all}",
       .usage = "Print value on specified ranks (default: 0)", },
    OPTPARSE_TABLE_END,
};
static struct optparse_option exchange_opts[] = {
    { .name = "count",      .has_arg = 1, .arginfo = "N",
       .usage = "Execute N exchange operations (default 1)", },
    OPTPARSE_TABLE_END,
};
static struct optparse_option general_opts[] = {
    { .name = "method",      .has_arg = 1, .arginfo = "URI",
      .usage = "Specify PMI method to use", },
    { .name = "libpmi-noflux", .has_arg = 0,
      .usage = "Fail if libpmi method finds the Flux libpmi.so", },
    { .name = "libpmi2-cray", .has_arg = 0,
      .usage = "Force-enable libpmi2 cray workarounds for testing", },
    { .name = "verbose",    .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Trace PMI operations", },
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
    { "exchange",
      "[OPTIONS]",
      "Perform an allgather style exchange",
      internal_cmd_exchange,
      0,
      exchange_opts,
    },
    OPTPARSE_SUBCMD_END
};

int subcommand_pmi_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p,
                                 "pmi",
                                 cmd_pmi,
                                 NULL,
                                 "Simple PMI test client",
                                 0,
                                 general_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    e = optparse_reg_subcommands (optparse_get_subcommand (p, "pmi"),
                                  pmi_subcmds);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}

// vi: ts=4 sw=4 expandtab
