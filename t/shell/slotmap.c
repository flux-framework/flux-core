/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>

#include <flux/optparse.h>

#include "src/shell/slotmap.h"

const char usage[] = "[OPTIONS] NTASKS";

static struct optparse_option opts[] = {
    { .name = "per-resource", .key = 'R',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Assign tasks per-resource instead of distributing. "
               "NAME is name of resource (node or core)",
    },
    { .name = "cores-per-slot", .key = 'c',
      .has_arg = 1, .arginfo = "N",
      .usage = "Explicitly set the number of cores per task",
    },
    { .name = "gpus-per-slot", .key = 'g',
      .has_arg = 1, .arginfo = "N",
      .usage = "Explicitly set the number of gpus per task",
    },
    { .name = "nslots", .key = 'n',
      .has_arg = 1, .arginfo = "N",
      .usage = "Number of slots to create if R has none (default: NTASKS)",
    },
    { .name = "label", .key = 'l',
      .has_arg = 1, .arginfo = "LABEL",
      .usage = "Distribute tasks only to slots with LABEL",
    },
    { .name = "slots", .key = 's',
      .has_arg = 0,
      .usage = "Also print the slots on each node",
    },
    { .name = "normalize", .key = 'N',
      .has_arg = 0,
      .usage = "Normalize resources to slots with tasks before printing",
    },
    OPTPARSE_TABLE_END
};

static void print_slots (struct slotmap *sm, int nodeid)
{
    json_t *slots;
    size_t index;
    json_t *slot;

    if (!(slots = slotmap_slots_json (sm, nodeid))) {
        fprintf (stderr, "slotmap_slots_json: %s\n", strerror (errno));
        exit (1);
    }
    json_array_foreach (slots, index, slot) {
        const char *label;
        const char *cores;
        const char *gpus;
        int ntasks;
        if (json_unpack (slot,
                         "{s:s s:i s:s s:s}",
                         "label", &label,
                         "ntasks", &ntasks,
                         "cores", &cores,
                         "gpus", &gpus) < 0) {
            fprintf (stderr, "failed to unpack slot\n");
            exit (1);
        }
        printf ("  slot: label=%s ntasks=%d cores=%s", label, ntasks, cores);
        if (strlen (gpus))
            printf (" gpus=%s", gpus);
        printf ("\n");
    }
    json_decref (slots);
}

static int nodes_used (struct slotmap *sm)
{
    int count = 0;
    for (int i = 0; i < slotmap_total_nodes (sm); i++) {
        struct slotmap_rankinfo ri;
        if (slotmap_get_nth (sm, i, &ri) == 0 && ri.ntasks > 0)
            count++;
    }
    return count;
}

int main (int ac, char **av)
{
    int i, ntasks, optindex;
    const char *rname = NULL;
    const char *label = NULL;
    struct slotmap *sm = NULL;
    json_t *R;
    flux_error_t error;

    optparse_t *p = optparse_create ("slotmap");
    if (p == NULL
        || optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS
        || optparse_set (p, OPTPARSE_USAGE, usage) != OPTPARSE_SUCCESS
        || optparse_parse_args (p, ac, av) < 0)
        exit (1);
    optindex = optparse_option_index (p);

    if (optindex != ac - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    optparse_getopt (p, "per-resource", &rname);
    optparse_getopt (p, "label", &label);

    if (!(R = json_loadf (stdin, 0, NULL))
        || !(sm = slotmap_create (R, &error)))  {
        fprintf (stderr, "Unable to create slotmap: %s\n", error.text);
        exit (1);
    }
    json_decref (R);
    if ((ntasks = strtoul (av[optindex], NULL, 10)) <= 0 || ntasks > 1e20) {
        fprintf (stderr, "Invalid value for ntasks: %s\n", av[1]);
        exit (1);
    }
    printf ("Distributing %d tasks%s%s across %d nodes with %d cores",
            ntasks,
            rname ? " per-" : "",
            rname ? rname : "",
            slotmap_total_nodes (sm),
            slotmap_total_cores (sm));
    if (slotmap_total_gpus (sm))
        printf (" %d gpus", slotmap_total_gpus (sm));
    printf ("\n");

    if (rname) {
        if (slotmap_set_slots_per_resource (sm, NULL, rname, &error) < 0) {
            fprintf (stderr,
                     "slotmap_set_slots_per_resource: %s\n",
                     error.text);
            exit (1);
        }
        ntasks *= slotmap_count_slots (sm, NULL);
    }
    else if (!slotmap_has_slots (sm)) {
        if (slotmap_set_slots_even (sm,
                                    NULL,
                                    optparse_get_int (p, "nslots", ntasks),
                                    optparse_get_int (p, "cores-per-slot", 0),
                                    optparse_get_int (p, "gpus-per-slot", 0),
                                    &error) < 0) {
            fprintf (stderr, "slotmap_set_slots_even: %s\n", error.text);
            exit (1);
        }
    }
    if (slotmap_distribute (sm,
                            label ? (const char *[]){ label, NULL } : NULL,
                            ntasks,
                            &error) < 0) {
        fprintf (stderr, "slotmap_distribute: %s\n", error.text);
        exit (1);
    }
    if (optparse_hasopt (p, "normalize")
        && slotmap_normalize (sm,
                              optparse_get_int (p, "cores-per-slot", 0),
                              optparse_get_int (p, "gpus-per-slot", 0)) < 0) {
        fprintf (stderr, "slotmap_normalize: %s\n", strerror (errno));
        exit (1);
    }
    printf ("Used %d nodes\n", nodes_used (sm));

    for (i = 0; i < slotmap_total_nodes (sm); i++) {
        struct slotmap_rankinfo ri;
        char *cores;
        char *gpus;
        if (slotmap_get_nth (sm, i, &ri) < 0) {
            fprintf (stderr, "slotmap_get_nth (rank=%d): %s\n",
                     i, strerror (errno));
            exit (1);
        }
        if (!(cores = slotmap_cores (sm, i))
            || !(gpus = slotmap_gpus (sm, i))) {
            fprintf (stderr, "failed to encode resources of rank %d\n", i);
            exit (1);
        }
        printf ("%d: rank=%d ntasks=%d cores=%s",
                ri.nodeid, ri.rank, ri.ntasks, cores);
        if (strlen (gpus))
            printf (" gpus=%s", gpus);
        printf ("\n");
        free (cores);
        free (gpus);
        if (optparse_hasopt (p, "slots"))
            print_slots (sm, i);
    }
    slotmap_destroy (sm);
    optparse_destroy (p);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
