/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_CLIQUE_H
#define _FLUX_CORE_PMI_CLIQUE_H

/* Parse the PMI_process_mapping attribute.
 *
 * The term "nodeid" below refers to a zero-origin logical nodeid within
 * the process group.  We can ask questions such
 *
 * - which nodeid will launch a given rank?
 * - how many procs will be launched on a given nodeid?
 * - which ranks will be launched on a given nodeid?
 *
 * N.B. due to the fixed PMI KVS value size, and the fact that a process
 * group can be mapped irregularly, some mappings may not be communicable
 * using this attribute.  Therefore, an empty value is to be interpreted
 * as "no mapping available", and should be handled as a non-fatal error.
 *
 * These functions return PMI result codes.
 */

struct pmi_map_block {
    int nodeid;
    int nodes;
    int procs;
};

/* Parse PMI_process_mapping value in 's' into an array of
 * struct pmi_map_blocks, returned in 'blocks', length in 'nblocks'.
 * The caller must free 'blocks'.
 */
int pmi_process_mapping_parse (const char *s,
                               struct pmi_map_block **blocks, int *nblocks);


/* Determine the nodeid that will start 'rank', and return it in 'nodeid'.
 */
int pmi_process_mapping_find_nodeid (struct pmi_map_block *blocks, int nblocks,
                                     int rank, int *nodeid);

/* Determine the number of ranks started by 'nodeid', and return it in 'nranks'.
 */
int pmi_process_mapping_find_nranks (struct pmi_map_block *blocks, int nblocks,
                                     int nodeid, int size, int *nranks);

/* Determine the ranks that will be started by 'nodeid'.
 * The caller should supply a pre-allocated array in 'ranks' of length
 * 'nranks', sized according to find_nranks() above.
 */
int pmi_process_mapping_find_ranks (struct pmi_map_block *blocks, int nblocks,
                                    int nodeid, int size,
                                    int *ranks, int nranks);


/* These functions can be used in place of PMI_Get_clique_size()
 * and PMI_Get_clique_ranks().  They make canonical PMI API calls
 * to obtain the rank, size, maxes, and PMI_process_mapping value.
 */
int pmi_process_mapping_get_clique_size (int *size);
int pmi_process_mapping_get_clique_ranks (int ranks[], int length);

#endif /* _FLUX_CORE_PMI_CLIQUE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
