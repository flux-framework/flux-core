/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_BROKER_PMIUTIL_H
#define HAVE_BROKER_PMIUTIL_H 1

struct pmi_params {
    int rank;
    int size;
    char kvsname[1024];
};

struct pmi_handle;

int broker_pmi_kvs_commit (struct pmi_handle *pmi, const char *kvsname);

int broker_pmi_kvs_put (struct pmi_handle *pmi,
                        const char *kvsname,
                        const char *key,
                        const char *value);

int broker_pmi_kvs_get (struct pmi_handle *pmi,
                        const char *kvsname,
                        const char *key,
                        char *value,
                        int len,
                        int from_rank); // -1 for undefined

int broker_pmi_barrier (struct pmi_handle *pmi);

int broker_pmi_get_params (struct pmi_handle *pmi, struct pmi_params *params);

int broker_pmi_init (struct pmi_handle *pmi);

int broker_pmi_finalize (struct pmi_handle *pmi);

void broker_pmi_destroy (struct pmi_handle *pmi);

struct pmi_handle *broker_pmi_create (void);

#endif /* !HAVE_BROKER_PMIUTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
