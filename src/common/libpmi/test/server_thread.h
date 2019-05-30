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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
