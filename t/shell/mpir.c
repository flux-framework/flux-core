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

#include "src/common/libutil/log.h"
#include "src/shell/mpir/proctable.h"

struct proctable *proctable = NULL;
MPIR_PROCDESC *MPIR_proctable = NULL;
int MPIR_proctable_size = 0;

static flux_jobid_t parse_jobid (const char *s)
{
    unsigned long long i;
    char *endptr;
    errno = 0;
    i = strtoull (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        log_msg_exit ("error parsing jobid '%s'\n", s);
    return (flux_jobid_t) i;
}

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
    flux_jobid_t id;
    int rank;
    char topic [1024];
    flux_t *h = NULL;
    flux_future_t *f = NULL;
    const char *s = NULL;

    log_init ("mpir-test");

    if (ac != 3)
        log_msg_exit ("Usage: %s JOBID LEADER-RANK\n", av [0]);
    id = parse_jobid (av[1]);
    rank = atoi (av[2]);

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    snprintf (topic, sizeof (topic), "shell-%ju.proctable", id);
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

