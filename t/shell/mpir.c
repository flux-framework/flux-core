/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* mpir.c - test for shell mpir/ptrace plugins */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/shell/mpir/proctable.h"

struct proctable *proctable = NULL;
MPIR_PROCDESC *MPIR_proctable = NULL;
int MPIR_proctable_size = 0;

static struct optparse_option opts[] = {
    { .name = "leader-rank",
      .key = 'r',
      .has_arg = 1,
      .arginfo = "RANK",
      .usage = "specify shell leader rank"
    },
    { .name = "service",
      .key = 's',
      .has_arg = 1,
      .arginfo = "NAME",
      .usage = "specify shell service NAME"
    },
    OPTPARSE_TABLE_END
};

static void set_mpir_proctable (const char *s)
{
    if (!(proctable = proctable_from_json_string (s)))
        log_err_exit ("proctable_from_json_string");
    MPIR_proctable = proctable_get_mpir_proctable (proctable,
                                                   &MPIR_proctable_size);
    if (!MPIR_proctable)
        log_err_exit ("proctable_get_mpir_proctable");
}

int main (int ac, char **av)
{
    int rank;
    char topic [1024];
    flux_t *h = NULL;
    flux_future_t *f = NULL;
    const char *s = NULL;
    const char *service;
    optparse_t *p;

    log_init ("mpir-test");
    if (!(p = optparse_create ("mpir-test"))
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_err_exit ("optparse_create");

    if (optparse_parse_args (p, ac, av) < 0)
        exit (1);

    rank = optparse_get_int (p, "leader-rank", -1);
    service = optparse_get_str (p, "service", NULL);
    if (rank < 0 || service == NULL)
        log_msg_exit ("--rank and --service are required");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    snprintf (topic, sizeof (topic), "%s.proctable", service);
    if (!(f = flux_rpc_pack (h, topic, rank, 0, "{}")))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get (f, &s) < 0)
        log_err_exit ("%s", topic);

    fprintf (stderr, "proctable=%s\n", s);

    set_mpir_proctable (s);

    proctable_destroy (proctable);
    flux_future_destroy (f);
    flux_close (h);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

