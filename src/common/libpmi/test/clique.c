/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <czmq.h>
#include <string.h>

#include "src/common/libpmi/clique.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libtap/tap.h"
#include "src/common/libutil/xzmalloc.h"

struct spec {
    char *vec;
    int size;
};

static struct spec valid[] = {
    /* flux rfc 13 */
    {"(vector,(0,16,16))", 256},  // [0]
    {"(vector,(0,8,16),(0,4,32))", 256},

    {"    (vector, (0, 16,16) )", 256},

    /* notes in openmpi code */
    /* c.f. opal/mca/pmix/s2/pmi2_pmap_parser.c */
    {"(vector,(0,4,4),(0,4,1))", 20},
    {"(vector,(0,2,1),(0,2,1))", 4},  // [4]
    {"(vector,(0,2,2))", 4},          // [5]

    /* mpich unit test */
    /* c.f. src/mpid/ch3/src/mpid_vc.c */
    {"(vector, (0,1,1))", 5},
    {"(vector, (0,1,1), (1,5,3), (6,2, 5))", 100},
    {"(vector, (1,1,1), (0,2,2))", 5},
    {"(vector, (1,1,1), (0,2,2),)", 5},
    {"", 1},

    /* grondo */
    {"(vector,(0,4,2),(1,3,1))", 10},

    {NULL, 0},
};

static struct spec invalid[] = {

    /* mpich unit test */
    /* c.f. src/mpid/ch3/src/mpid_vc.c */
    {"XXX, (1,1))", 1},
    {"vector, (1,1))", 1},
    {"(vector, (1.11, 2,2))", 1},

    {NULL, 0},
};

static char *cliquetostr (int rank, int len, int *clique)
{
    int i, slen = (len + 1) * 16;
    char *s;

    if (len == 0)
        return NULL;

    s = xzmalloc (slen);
    sprintf (s, "%d: ", rank);
    for (i = 0; i < len; i++)
        sprintf (s + strlen (s), "%s%d", i > 0 ? "," : "", clique[i]);
    return s;
}

static char *cliqueN (struct pmi_map_block *blocks,
                      int nblocks,
                      int size,
                      int rank)
{
    int rc;
    int nranks;
    int *ranks = NULL;
    char *s;

    rc = pmi_process_mapping_find_nranks (blocks, nblocks, rank, size, &nranks);
    if (rc != PMI_SUCCESS)
        return NULL;
    ranks = xzmalloc (sizeof (int) * nranks);
    rc = pmi_process_mapping_find_ranks (blocks,
                                         nblocks,
                                         rank,
                                         size,
                                         ranks,
                                         nranks);
    if (rc != PMI_SUCCESS) {
        free (ranks);
        return NULL;
    }
    s = cliquetostr (rank, nranks, ranks);
    free (ranks);
    return s;
}

int main (int argc, char *argv[])
{
    int nblocks, nodeid;
    int nranks, *ranks;
    struct pmi_map_block *blocks = NULL;
    int i, rank, rc;
    char *s;

    plan (NO_PLAN);

    /* Check the parser.
     */

    rc = pmi_process_mapping_parse (valid[0].vec, &blocks, &nblocks);
    ok (rc == PMI_SUCCESS && nblocks == 1 && blocks[0].nodeid == 0
            && blocks[0].nodes == 16 && blocks[0].procs == 16,
        "correctly parsed single-block vector");
    if (rc == PMI_SUCCESS)
        free (blocks);

    rc = pmi_process_mapping_parse (valid[1].vec, &blocks, &nblocks);
    ok (rc == PMI_SUCCESS && nblocks == 2 && blocks[0].nodeid == 0
            && blocks[0].nodes == 8 && blocks[0].procs == 16
            && blocks[1].nodeid == 0 && blocks[1].nodes == 4
            && blocks[1].procs == 32,
        "correctly parsed 2-block vector");
    if (rc == PMI_SUCCESS)
        free (blocks);

    rc = pmi_process_mapping_parse (valid[2].vec, &blocks, &nblocks);
    ok (rc == PMI_SUCCESS && nblocks == 1 && blocks[0].nodeid == 0
            && blocks[0].nodes == 16 && blocks[0].procs == 16,
        "correctly parsed single-block vector with whitespace");
    if (rc == PMI_SUCCESS)
        free (blocks);

    /* Detailed check of regular cyclic layout
     */

    rc = pmi_process_mapping_parse (valid[4].vec, &blocks, &nblocks);
    ok (rc == PMI_SUCCESS, "parsed cyclic layout of 4 procs on 2 nodes");
    if (rc != PMI_SUCCESS)
        BAIL_OUT ("cannot continue");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 0, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 0,
        "find_nodeid says node 0 runs proc 0");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 1, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 1,
        "find_nodeid says node 1 runs proc 1");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 2, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 0,
        "find_nodeid says node 0 runs proc 2");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 3, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 1,
        "find_nodeid says node 1 runs proc 3");

    rc = pmi_process_mapping_find_nranks (blocks,
                                          nblocks,
                                          0,
                                          valid[4].size,
                                          &nranks);
    ok (rc == PMI_SUCCESS && nranks == 2,
        "find_nranks says node 0 runs two procs");
    ranks = xzmalloc (sizeof (int) * nranks);
    rc = pmi_process_mapping_find_ranks (blocks,
                                         nblocks,
                                         0,
                                         valid[4].size,
                                         ranks,
                                         nranks);
    ok (rc == PMI_SUCCESS && ranks[0] == 0 && ranks[1] == 2,
        "find_ranks says node 0 runs 0,2");
    free (ranks);

    rc = pmi_process_mapping_find_nranks (blocks,
                                          nblocks,
                                          1,
                                          valid[4].size,
                                          &nranks);
    ok (rc == PMI_SUCCESS && nranks == 2,
        "find_nranks says node 1 runs two procs");
    ranks = xzmalloc (sizeof (int) * nranks);
    rc = pmi_process_mapping_find_ranks (blocks,
                                         nblocks,
                                         1,
                                         valid[4].size,
                                         ranks,
                                         nranks);
    ok (rc == PMI_SUCCESS && ranks[0] == 1 && ranks[1] == 3,
        "find_ranks says node 1 runs 1,3");
    free (ranks);

    free (blocks);

    /* Detailed check of regular block layout
     */

    rc = pmi_process_mapping_parse (valid[5].vec, &blocks, &nblocks);
    ok (rc == PMI_SUCCESS, "parsed block layout of 4 procs on 2 nodes");
    if (rc != PMI_SUCCESS)
        BAIL_OUT ("cannot continue");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 0, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 0,
        "find_nodeid says node 0 runs proc 0");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 1, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 0,
        "find_nodeid says node 0 runs proc 1");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 2, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 1,
        "find_nodeid says node 1 runs proc 2");
    rc = pmi_process_mapping_find_nodeid (blocks, nblocks, 3, &nodeid);
    ok (rc == PMI_SUCCESS && nodeid == 1,
        "find_nodeid says node 1 runs proc 3");

    rc = pmi_process_mapping_find_nranks (blocks,
                                          nblocks,
                                          0,
                                          valid[5].size,
                                          &nranks);
    ok (rc == PMI_SUCCESS && nranks == 2,
        "find_nranks says node 0 runs two procs");
    ranks = xzmalloc (sizeof (int) * nranks);
    rc = pmi_process_mapping_find_ranks (blocks,
                                         nblocks,
                                         0,
                                         valid[5].size,
                                         ranks,
                                         nranks);
    ok (rc == PMI_SUCCESS && ranks[0] == 0 && ranks[1] == 1,
        "find_ranks says node 0 runs 0,1");
    free (ranks);

    rc = pmi_process_mapping_find_nranks (blocks,
                                          nblocks,
                                          1,
                                          valid[5].size,
                                          &nranks);
    ok (rc == PMI_SUCCESS && nranks == 2,
        "find_nranks says node 1 runs two procs");
    ranks = xzmalloc (sizeof (int) * nranks);
    rc = pmi_process_mapping_find_ranks (blocks,
                                         nblocks,
                                         1,
                                         valid[5].size,
                                         ranks,
                                         nranks);
    ok (rc == PMI_SUCCESS && ranks[0] == 2 && ranks[1] == 3,
        "find_ranks says node 1 runs 2,3");
    free (ranks);
    free (blocks);

    /* Valid
     */

    for (i = 0; valid[i].vec != NULL; i++) {
        rc = pmi_process_mapping_parse (valid[i].vec, &blocks, &nblocks);
        ok (rc == PMI_SUCCESS,
            "parsed %s size=%d",
            valid[i].vec,
            valid[i].size);
        for (rank = 0; rank < valid[i].size; rank++) {
            s = cliqueN (blocks, nblocks, valid[i].size, rank);
            if (!s)
                break;
            diag ("%s", s);
            free (s);
        }
        free (blocks);
    }

    /* Invalid
     */

    for (i = 0; invalid[i].vec != NULL; i++) {
        rc = pmi_process_mapping_parse (invalid[i].vec, &blocks, &nblocks);
        ok (rc == PMI_FAIL, "refused to parse %s", invalid[i].vec);
    }

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
