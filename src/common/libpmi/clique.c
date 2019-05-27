/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <argz.h>

#include "pmi.h"
#include "clique.h"

static int parse_block (const char *s, struct pmi_map_block *block)
{
    char *endptr;

    errno = 0;
    block->nodeid = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ',')
        return PMI_FAIL;
    s = endptr + 1;
    errno = 0;
    block->nodes = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ',')
        return PMI_FAIL;
    s = endptr + 1;
    errno = 0;
    block->procs = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != ')')
        return PMI_FAIL;
    return PMI_SUCCESS;
}

int pmi_process_mapping_parse (const char *s,
                               struct pmi_map_block **blocksp, int *nblocksp)
{
    char *argz = NULL;
    size_t argz_len;
    char *entry = NULL;
    int nblocks, i;
    struct pmi_map_block *blocks = NULL;
    int rc = PMI_FAIL;

    /* Special case empty string.
     * Not an error: return no blocks.
     */
    if (*s == '\0') {
        *blocksp = NULL;
        *nblocksp = 0;
        return PMI_SUCCESS;
    }

    if (argz_create_sep (s, '(', &argz, &argz_len) != 0) {
        rc = PMI_ERR_NOMEM;
        goto error;
    }
    nblocks = argz_count (argz, argz_len);
    while ((entry = argz_next (argz, argz_len, entry))) {
        nblocks--;
        if (strstr (entry, "vector,"))
            break;
    }
    if (nblocks == 0)
        goto error;
    if (!(blocks = calloc (nblocks, sizeof (blocks[0])))) {
        rc = PMI_ERR_NOMEM;
        goto error;
    }
    i = 0;
    while ((entry = argz_next (argz, argz_len, entry))) {
        if ((rc = parse_block (entry, &blocks[i++])) != PMI_SUCCESS)
            goto error;
    }
    *nblocksp = nblocks;
    *blocksp = blocks;
    free (argz);
    return PMI_SUCCESS;
error:
    if (blocks)
        free (blocks);
    if (argz)
        free (argz);
    return rc;
}

int pmi_process_mapping_find_nodeid (struct pmi_map_block *blocks, int nblocks,
                                     int rank, int *nodeid)
{
    int i;
    int brank = 0;

    for (i = 0; i < nblocks; i++) {
        int lsize = blocks[i].nodes * blocks[i].procs;
        int lrank = rank - brank;

        if (lrank >= 0 && lrank < lsize) {
            *nodeid = blocks[i].nodeid + lrank / blocks[i].procs;
            return PMI_SUCCESS;
        }
        brank += lsize;
    }
    return PMI_FAIL;
}

int pmi_process_mapping_find_nranks (struct pmi_map_block *blocks, int nblocks,
                                     int nodeid, int size, int *nranksp)
{
    int i, j;
    int brank = 0, nranks = 0;

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < blocks[i].nodes; j++) {
            int lsize = blocks[i].procs;
            if (brank + lsize > size)
                lsize -= (brank - size);
            if (blocks[i].nodeid + j == nodeid)
                nranks += lsize;
            brank += lsize;
        }
    }
    *nranksp = nranks;
    return PMI_SUCCESS;
}

int pmi_process_mapping_find_ranks (struct pmi_map_block *blocks, int nblocks,
                                    int nodeid, int size,
                                    int *ranks, int nranks)
{
    int i, j, k, nx = 0;
    int brank = 0;

    for (i = 0; i < nblocks; i++) {
        for (j = 0; j < blocks[i].nodes; j++) {
            int lsize = blocks[i].procs;
            if (brank + lsize > size)
                lsize -= (brank - size);
            if (blocks[i].nodeid + j == nodeid) {
                for (k = 0; k < lsize; k++) {
                    if (nx >=  nranks)
                        return PMI_ERR_INVALID_SIZE;
                    ranks[nx++] = brank + k;
                }
            }
            brank += lsize;
        }
    }
    if (nx != nranks)
        return PMI_ERR_INVALID_SIZE;
    return PMI_SUCCESS;
}

/* Emulation of PMI_Get_clique_size() and PMI_Get_clique_ranks().
 */

struct clique_context {
    int rank;
    int size;
    int name_max;
    int val_max;
    char *kvsname;
    char *value;
    struct pmi_map_block *blocks;
    int nblocks;
    int nodeid;
};

static int clique_context_init (struct clique_context *ctx)
{
    int rc = PMI_FAIL;

    ctx->blocks = NULL;
    ctx->kvsname = NULL;
    rc = PMI_Get_rank (&ctx->rank);
    if (rc != PMI_SUCCESS)
        goto done;
    rc = PMI_Get_size (&ctx->size);
    if (rc != PMI_SUCCESS)
        goto done;
    rc = PMI_KVS_Get_name_length_max (&ctx->name_max);
    if (rc != PMI_SUCCESS)
        goto done;
    if (!(ctx->kvsname = calloc (1, ctx->name_max))) {
        rc = PMI_ERR_NOMEM;
        goto done;
    }
    rc = PMI_KVS_Get_value_length_max (&ctx->val_max);
    if (rc != PMI_SUCCESS)
        goto done;
    if (!(ctx->value = calloc (1, ctx->val_max))) {
        rc = PMI_ERR_NOMEM;
        goto done;
    }
    rc = PMI_KVS_Get_my_name (ctx->kvsname, ctx->name_max);
    if (rc != PMI_SUCCESS)
        goto done;
    rc = PMI_KVS_Get (ctx->kvsname, "PMI_process_mapping",
                      ctx->value, ctx->val_max);
    if (rc != PMI_SUCCESS)
        goto done;
    rc = pmi_process_mapping_parse (ctx->value, &ctx->blocks, &ctx->nblocks);
    if (rc != PMI_SUCCESS)
        goto done;
    if (pmi_process_mapping_find_nodeid (ctx->blocks, ctx->nblocks, ctx->rank,
                                         &ctx->nodeid) != PMI_SUCCESS) {
        ctx->nodeid = -1; /* not found - not an error */
    }
done:
    return rc;
}

static void clique_context_finalize (struct clique_context *ctx)
{
    if (ctx->kvsname) {
        free (ctx->kvsname);
        ctx->kvsname = NULL;
    }
    if (ctx->value) {
        free (ctx->value);
        ctx->value = NULL;
    }
    if (ctx->blocks) {
        free (ctx->blocks);
        ctx->blocks = NULL;
    }
}

int pmi_process_mapping_get_clique_size (int *size)
{
    struct clique_context ctx;
    int rc;

    rc = clique_context_init (&ctx);
    if (rc != PMI_SUCCESS)
        goto done;
    if (ctx.nodeid == -1) {
        *size = 1;
    } else {
        rc = pmi_process_mapping_find_nranks (ctx.blocks, ctx.nblocks,
                                              ctx.nodeid, ctx.size, size);
    }
done:
    clique_context_finalize (&ctx);
    return rc;
}

int pmi_process_mapping_get_clique_ranks (int ranks[], int length)
{
    struct clique_context ctx;
    int rc;

    rc = clique_context_init (&ctx);
    if (rc != PMI_SUCCESS)
        goto done;
    if (ctx.nodeid == -1) {
        if (length < 1) {
            rc = PMI_ERR_INVALID_SIZE;
            goto done;
        }
        *ranks = ctx.rank;
    } else {
        rc = pmi_process_mapping_find_ranks (ctx.blocks, ctx.nblocks,
                                             ctx.nodeid, ctx.size,
                                             ranks, length);
    }
done:
    clique_context_finalize (&ctx);
    return rc;
}

char *pmi_cliquetostr (char *buf, int bufsz, int *ranks, int length)
{
    int n, i, count;

    buf[0] = '\0';
    for (i = 0, count  = 0; i < length; i++) {
        n = snprintf (buf + count,
                      bufsz - count,
                      "%s%d",
                      i > 0 ? "," : "",
                      ranks[i]);
        if (n >= bufsz - count)
            return "overflow";
        count += n;
    }
    return buf;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
