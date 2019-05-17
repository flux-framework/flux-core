/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _KVS_DIR_PRIVATE_H
#    define _KVS_DIR_PRIVATE_H

flux_kvsdir_t *kvsdir_create_fromobj (flux_t *handle,
                                      const char *rootref,
                                      const char *key,
                                      json_t *treeobj);

json_t *kvsdir_get_obj (flux_kvsdir_t *dir);

#endif /* !_KVS_DIR_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
