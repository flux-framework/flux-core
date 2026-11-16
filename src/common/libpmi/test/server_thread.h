/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

struct pmi_server_context *pmi_server_create (int *cfd, int size);
void pmi_server_destroy (struct pmi_server_context *ctx);

void pmi_set_barrier_entry_failure (struct pmi_server_context *ctx, int val);
void pmi_set_barrier_exit_failure (struct pmi_server_context *ctx, int val);
void pmi_set_barrier_out_tag (struct pmi_server_context *ctx, int tag);

/* Return the number of barrier_in lines the server has seen from 'rank'.
 */
int pmi_server_barrier_in_count (struct pmi_server_context *ctx, int rank);

/* Spin until the server has seen more than 'count' barrier_in lines
 * from 'rank' and the latest one's group string is 'group' (NULL for the
 * untagged whole-job barrier).  Take 'count' before starting the thread
 * that sends, so a line left over from an earlier barrier cannot satisfy
 * the wait.
 */
void pmi_server_wait_barrier_in (struct pmi_server_context *ctx,
                                 int rank,
                                 const char *group,
                                 int count);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
