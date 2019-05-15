/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_OPERATIONS_H
#define _FLUX_CORE_PMI_OPERATIONS_H

#include "src/common/libpmi/pmi.h"

struct pmi_operations {
    int (*init) (void *impl, int *spawned);
    int (*initialized) (void *impl, int *initialized);
    int (*finalize) (void *impl);
    int (*get_size) (void *impl, int *size);
    int (*get_rank) (void *impl, int *rank);
    int (*get_appnum) (void *impl, int *appnum);
    int (*get_universe_size) (void *impl, int *universe_size);
    int (*publish_name) (void *impl, const char *service_name, const char *port);
    int (*unpublish_name) (void *impl, const char *service_name);
    int (*lookup_name) (void *impl, const char *service_name, char *port);
    int (*barrier) (void *impl);
    int (*abort) (void *impl, int exit_code, const char *error_msg);
    int (*kvs_get_my_name) (void *impl, char *kvsname, int length);
    int (*kvs_get_name_length_max) (void *impl, int *length);
    int (*kvs_get_key_length_max) (void *impl, int *length);
    int (*kvs_get_value_length_max) (void *impl, int *length);
    int (*kvs_put) (void *impl,
                    const char *kvsname,
                    const char *key,
                    const char *value);
    int (*kvs_commit) (void *impl, const char *kvsname);
    int (*kvs_get) (void *impl,
                    const char *kvsname,
                    const char *key,
                    char *value,
                    int len);
    int (*get_clique_size) (void *impl, int *size);
    int (*get_clique_ranks) (void *impl, int ranks[], int length);

    int (*spawn_multiple) (void *impl,
                           int count,
                           const char *cmds[],
                           const char **argvs[],
                           const int maxprocs[],
                           const int info_keyval_sizesp[],
                           const PMI_keyval_t *info_keyval_vectors[],
                           int preput_keyval_size,
                           const PMI_keyval_t preput_keyval_vector[],
                           int errors[]);

    void (*destroy) (void *impl);
};

#endif /* _FLUX_CORE_PMI_OPERATIONS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
