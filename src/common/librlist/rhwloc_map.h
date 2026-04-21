/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_RHWLOC_MAP_H
#define HAVE_RHWLOC_MAP_H 1

typedef struct rhwloc_map rhwloc_map_t;

/*  Create a resource mapping context from hwloc topology XML.
 */
rhwloc_map_t *rhwloc_map_create (const char *xml);
void rhwloc_map_destroy (rhwloc_map_t *m);

/*  Map logical core IDs (idset string) to AllowedCPUs and AllowedMemoryNodes
 *  idset strings.  On success returns 0 with *cpus_out and *mems_out set to
 *  heap-allocated strings the caller must free().  Returns -1 on error.
 */
int rhwloc_map_cores (rhwloc_map_t *m,
                      const char *cores,
                      char **cpus_out,
                      char **mems_out);

/*  Map logical GPU IDs (idset string) to a NULL-terminated array of PCI
 *  address strings in the same order as the input GPU IDs.  Returns NULL
 *  on error.  Caller must free with rhwloc_map_strv_free().
 */
char **rhwloc_map_gpu_pci_addrs (rhwloc_map_t *m, const char *gpus);

/*  Free a NULL-terminated string array.
 */
void rhwloc_map_strv_free (char **strv);

#endif /* !HAVE_RHWLOC_MAP_H */
