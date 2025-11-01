/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "jobspec-info"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libtap/tap.h"

static int die (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    return -1;
}

static int check_jobspec_info (flux_plugin_t *p,
                               const char *topic,
                               flux_plugin_arg_t *args,
                               void *data)
{
    int version;
    int ntasks, ntasks_expected;
    int nslots, nslots_expected;
    int cores_per_slot, cps_expected;
    int gpus_per_slot, gps_expected;
    int nnodes, nnodes_expected;
    int slots_per_node, spn_expected;
    int node_exclusive, node_exclusive_expected;
    char *json_str = NULL;
    int rc;

    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!p)
        return die ("flux_plugin_get_shell\n");

    rc = flux_shell_get_jobspec_info (shell, &json_str);
    ok (rc == 0,
        "flux_shell_get_jobspec_info works");
    ok (json_str && strlen (json_str) > 0,
        "flux_shell_get_jobspec_info returns a JSON string with len > 0");

    rc = flux_shell_getopt_unpack (shell,
                                   "jobspec_info",
                                   "{s:i s:i s:i s:i s:i s:i s:b}",
                                   "ntasks", &ntasks_expected,
                                   "nnodes", &nnodes_expected,
                                   "nslots", &nslots_expected,
                                   "cores_per_slot", &cps_expected,
                                   "gpus_per_slot", &gps_expected,
                                   "slots_per_node", &spn_expected,
                                   "node_exclusive", &node_exclusive_expected);
    if (rc < 0)
        return die ("flux_shell_getopt_unpack: %s\n", strerror (errno));

    rc = flux_shell_jobspec_info_unpack (shell,
                                         "{s:i s:i s:i s:i s:i s:i s:i s:b}",
                                         "version", &version,
                                         "ntasks", &ntasks,
                                         "nslots", &nslots,
                                         "cores_per_slot", &cores_per_slot,
                                         "gpus_per_slot", &gpus_per_slot,
                                         "nnodes", &nnodes,
                                         "slots_per_node", &slots_per_node,
                                         "node_exclusive", &node_exclusive
                                    );
    ok (rc == 0,
        "flux_jobspec_info_unpack works");

    ok (version == 1,
        "version is reported as 1 (got %d)",
        version);
    ok (ntasks == ntasks_expected,
        "ntasks (%d) has expected value (%d)", ntasks, ntasks_expected);
    ok (nnodes == nnodes_expected,
        "nnodes (%d) has expected value (%d)", nnodes, nnodes_expected);
    ok (nslots == nslots_expected,
        "nslots (%d) has expected value (%d)", nslots, nslots_expected);
    ok (cores_per_slot == cps_expected,
        "cores_per_slot (%d) has expected value (%d)",
        cores_per_slot,
        cps_expected);
    ok (gpus_per_slot == gps_expected,
        "gpus_per_slot (%d) has expected value (%d)",
        gpus_per_slot,
        gps_expected);
    ok (slots_per_node == spn_expected,
        "slots_per_node (%d) has expected value (%d)",
        slots_per_node,
        spn_expected);
    ok (node_exclusive == node_exclusive_expected,
        "node_exclusive (%s) has expected value (%s)",
        node_exclusive ? "true" : "false",
        node_exclusive_expected ? "true" : "false");

    return exit_status () == 0 ? 0 : -1;
}

int flux_plugin_init (flux_plugin_t *p)
{
    plan (NO_PLAN);
    ok (flux_plugin_add_handler (p, "shell.init",
                                 check_jobspec_info,
                                 NULL) == 0,
        "flux_plugin_add_handler works");
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
