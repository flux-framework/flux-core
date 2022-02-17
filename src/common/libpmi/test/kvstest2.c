/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>

#include "src/common/libutil/log.h"
#include "src/common/libpmi/pmi2.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libpmi/clique.h"

/* We don't have a pmi2_strerror() but the codes are mostly the same as PMI-1
 */
#define pmi2_strerror pmi_strerror

int main(int argc, char *argv[])
{
    int size, rank;
    char jobid[PMI2_MAX_VALLEN];
    char key[PMI2_MAX_KEYLEN];
    char val[PMI2_MAX_VALLEN];
    char map[PMI2_MAX_ATTRVALUE];
    char attr[PMI2_MAX_ATTRVALUE];
    char expected_attr[PMI2_MAX_ATTRVALUE];
    char expected_val[PMI2_MAX_VALLEN];
    const int keycount = 10;
    struct pmi_map_block *blocks;
    int nblocks;
    int nranks;
    int *ranks;
    int e;
    int length;
    int clique_nodeid;
    int clique_rank;
    int clique_neighbor;

    /* Initialize
     */
    e = PMI2_Init (NULL, &size, &rank, NULL);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("PMI2_Init: %s", pmi2_strerror (e));
    e = PMI2_Job_GetId (jobid, sizeof (jobid));
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Job_Getid: %s", rank, pmi2_strerror (e));

    /* Parse PMI_process_mapping, setting nranks (number of ranks in clique),
     * and ranks[] (array of ranks in clique).
     */
    e = PMI2_Info_GetJobAttr ("PMI_process_mapping", map, sizeof (map), NULL);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetJobAttr PMI_process_mapping: %s",
                      rank, pmi2_strerror (e));
    e = pmi_process_mapping_parse (map, &blocks, &nblocks);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: error parsing PMI_process_mapping: %s",
                      rank, pmi2_strerror (e));
    e = pmi_process_mapping_find_nodeid (blocks, nblocks, rank, &clique_nodeid);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: error finding my clique nodeid: %s",
                      rank, pmi2_strerror (e));
    e = pmi_process_mapping_find_nranks (blocks,
                                         nblocks,
                                         clique_nodeid,
                                         size,
                                         &nranks);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: error finding size of clique: %s",
                      rank, pmi2_strerror (e));
    if (!(ranks = calloc (nranks, sizeof (ranks[0]))))
        log_err_exit ("calloc");
    e = pmi_process_mapping_find_ranks (blocks,
                                        nblocks,
                                        clique_nodeid,
                                        size,
                                        ranks,
                                        nranks);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: error finding members of clique: %s",
                      rank, pmi2_strerror (e));

    /* Set clique_rank to this rank's index in ranks[]
     */
    clique_rank = -1;
    for (int i = 0; i < nranks; i++) {
        if (ranks[i] == rank)
            clique_rank = i;
    }
    if (clique_rank == -1)
        log_msg_exit ("%d: unable to determine clique rank", rank);

    /* Exchange node-scope keys.
     * Each rank puts one key, then fetches the key of clique neighbor.
     * N.B. keys deliberately overlap across cliques.
     */
    snprintf (key, sizeof (key), "key-%d", clique_rank);
    snprintf (attr, sizeof (attr), "val-%d", rank);
    e = PMI2_Info_PutNodeAttr (key, attr);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_PutNodeAttr: %s", rank, pmi2_strerror (e));

    clique_neighbor = clique_rank > 0 ? clique_rank - 1 : nranks - 1;
    snprintf (key, sizeof (key), "key-%d", clique_neighbor);
    snprintf (expected_attr, sizeof (expected_attr), "val-%d",
              ranks[clique_neighbor]);
    e = PMI2_Info_GetNodeAttr (key, attr, sizeof (attr), NULL, 1);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetNodeAttr %s: %s",
                      rank, key, pmi2_strerror (e));
    if (strcmp (attr, expected_attr) != 0)
        log_msg_exit ("%d: PMI_Info_GetNodeAttr %s: exp %s got %s\n",
                      rank, key, expected_val, val);

    /* Put some keys; Fence; Get neighbor's keys.
     */
    for (int i = 0; i < keycount; i++) {
        snprintf (key, sizeof (key), "key-%d-%d", rank, i);
        snprintf (val, sizeof (val), "val-%d.%d", rank, i);
        e = PMI2_KVS_Put (key, val);
        if (e != PMI2_SUCCESS)
            log_msg_exit ("%d: PMI2_KVS_Put: %s", rank, pmi2_strerror (e));
    }
    e = PMI2_KVS_Fence();
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_KVS_Fence: %s", rank, pmi2_strerror (e));
    for (int i = 0; i < keycount; i++) {
        snprintf (key, sizeof (key), "key-%d-%d",
                  rank > 0 ? rank - 1 : size - 1, i);
        e = PMI2_KVS_Get (jobid, 0, key, val, sizeof (val), &length);
        if (e != PMI2_SUCCESS)
            log_msg_exit ("%d: PMI2_KVS_Get: %s", rank, pmi2_strerror (e));
        snprintf (expected_val, sizeof (expected_val), "val-%d.%d",
                  rank > 0 ? rank - 1 : size - 1, i);
        if (strcmp (val, expected_val) != 0)
            log_msg_exit ("%d: PMI_KVS_Get: exp %s got %s\n",
                          rank, expected_val, val);
        if (length != strlen (val))
            log_msg_exit ("%d: PMI_KVS_Get %s: length %d != expected %zd",
                          rank, key, length, strlen (val));
    }

    /* Finalize
     */
    e = PMI2_Finalize ();
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Finalize: %s", rank, pmi2_strerror (e));

    free (ranks);
    free (blocks);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
