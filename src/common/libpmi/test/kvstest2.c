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
#include <flux/taskmap.h>

#include "src/common/libutil/log.h"
#include "src/common/libpmi/pmi2.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "ccan/str/str.h"

/* We don't have a pmi2_strerror() but the codes are mostly the same as PMI-1
 */
#define pmi2_strerror pmi_strerror

static int find_id (const struct idset *ids, unsigned int id)
{
    unsigned int i;
    int index = 0;

    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (i == id)
            return index;
        i = idset_next (ids, i);
        index++;
    }
    return -1;
}

static int get_neighbor (const struct idset *ids, unsigned int id)
{
    int i = idset_next (ids, id);
    if (i == IDSET_INVALID_ID)
        return idset_first (ids);
    return i;
}

int main(int argc, char *argv[])
{
    int size, rank;
    char jobid[PMI2_MAX_VALLEN];
    char key[PMI2_MAX_KEYLEN];
    char val[PMI2_MAX_VALLEN];
    char attr[PMI2_MAX_ATTRVALUE];
    char expected_attr[PMI2_MAX_ATTRVALUE];
    char expected_val[PMI2_MAX_VALLEN];
    const int keycount = 10;
    struct taskmap *map;
    const struct idset *taskids;
    flux_error_t error;
    int e;
    int length;
    int nodeid;
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

    /* Parse PMI_process_mapping, get this rank's nodeid and clique size
     */
    e = PMI2_Info_GetJobAttr ("PMI_process_mapping", val, sizeof (val), NULL);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetJobAttr PMI_process_mapping: %s",
                      rank, pmi2_strerror (e));

    if (!(map = taskmap_decode (val, &error)))
        log_msg_exit ("%d: error parsing PMI_process_mapping: %s",
                      rank, error.text);
    if ((nodeid = taskmap_nodeid (map, rank)) < 0)
        log_msg_exit ("%d: failed to get this rank's nodeid: %s",
                      rank, strerror (errno));
    if (!(taskids = taskmap_taskids (map, nodeid)))
        log_msg_exit ("%d: failed to get taskids for node %d: %s",
                      rank, nodeid, strerror (errno));

    /* Set clique_rank to this rank's position in taskids
     */
    clique_rank = find_id (taskids, rank);
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

    clique_neighbor = get_neighbor (taskids, rank);
    snprintf (key, sizeof (key), "key-%d", clique_neighbor);
    snprintf (expected_attr, sizeof (expected_attr), "val-%d",
              clique_neighbor);
    e = PMI2_Info_GetNodeAttr (key, attr, sizeof (attr), NULL, 1);
    if (e != PMI2_SUCCESS)
        log_msg_exit ("%d: PMI2_Info_GetNodeAttr %s: %s",
                      rank, key, pmi2_strerror (e));
    if (!streq (attr, expected_attr))
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
        if (!streq (val, expected_val))
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


    taskmap_destroy (map);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
