/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_SIMPLE_CLIENT_H
#define _FLUX_CORE_PMI_SIMPLE_CLIENT_H

#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

#include "src/common/libutil/aux.h"
#include "src/common/libflux/types.h"

struct pmi_simple_client {
    // valid upon creation
    int rank;
    int size;
    int spawned;

    // valid after client_init()
    int initialized;
    unsigned int version;       // server's negotiated PMI version
    unsigned int subversion;    // server's maximum PMI subversion
    unsigned int kvsname_max;
    unsigned int keylen_max;
    unsigned int vallen_max;

    // for internal pmi_simple_client use only
    int fd;
    FILE *f;                    // write side (stdio); reads use fd directly
    char *buf;                  // current response line
    int buflen;
    char *rbuf;                 // raw input buffer for read-ahead
    size_t rbufsize;
    size_t rlen;                // bytes of unread input in rbuf
    pthread_mutex_t lock;       // serializes wire I/O and the fields above
    struct barrier_out *pending;  // barrier_out responses read for other tags
    struct barrier_wait *waiting;  // barriers in flight
    pthread_cond_t cond;  // signaled when pending or reader changes
    bool reader;  // a barrier waiter is polling the fd
    bool failed;  // barrier replies can no longer be matched to waiters
    int wakefd[2];  // pipe that wakes the reader from poll
    struct aux_item *aux;
};

/* Create/destroy
 * On error, create returns NULL and sets errno.
 * Required: pmi_fd, pmi_rank, and pmi_size from the environment.
 * Optional: pmi_spawned from the environment (may be NULL).
 */
void pmi_simple_client_destroy (struct pmi_simple_client *pmi);
struct pmi_simple_client *pmi_simple_client_create_fd (const char *pmi_fd,
                                                       const char *pmi_rank,
                                                       const char *pmi_size,
                                                       const char *pmi_spawned);

/* Core operations
 */
int pmi_simple_client_init (struct pmi_simple_client *pmi);
int pmi_simple_client_finalize (struct pmi_simple_client *pmi);
int pmi_simple_client_get_appnum (struct pmi_simple_client *pmi, int *appnum);
int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size);

void *pmi_simple_client_aux_get (struct pmi_simple_client *pmi,
                                 const char *name);
int pmi_simple_client_aux_set (struct pmi_simple_client *pmi,
                               const char *name,
                               void *aux,
                               flux_free_f destroy);

/* Synchronization
 */
int pmi_simple_client_barrier (struct pmi_simple_client *pmi);

/* PMI-1.2 group barrier (RFC 13).  'group' is "WORLD", "NODE", or a
 * comma-separated list of ranks.  'tag' is a non-negative integer the
 * server echoes in the response.  Returns PMI_FAIL if the server did
 * not offer subversion 2.  PMI_Barrier_group() in pmi.c wraps this.
 *
 * Group barriers with distinct tags may be issued concurrently from
 * multiple threads and interleaved with other operations, including
 * pmi_simple_client_barrier(), which is sent untagged.  A barrier
 * entered while another with the same tag (or another whole-job
 * barrier) is in flight on this client fails with PMI_FAIL.  Once a
 * barrier fails after it was sent, every later barrier fails too.
 */
int pmi_simple_client_barrier_group (struct pmi_simple_client *pmi,
                                     const char *group,
                                     int tag);

/* Hash a string tag to the integer wire tag as MPICH does (32-bit
 * FNV-1a, cast to int, made non-negative).  NULL maps to 0.
 */
int pmi_simple_client_barrier_tag (const char *stringtag);

/* KVS operations
 */
int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname,
                                       int length);
int pmi_simple_client_kvs_put (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               const char *value);
int pmi_simple_client_kvs_get (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key,
                               char *value,
                               int len);

/* Clique functions emulated with PMI_process_mapping key.
 */
int pmi_simple_client_get_clique_size (struct pmi_simple_client *pmi,
                                       int *size);
int pmi_simple_client_get_clique_ranks (struct pmi_simple_client *pmi,
                                        int ranks[],
                                        int length);

/* Abort
 */
int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code,
                             const char *msg);

/* Not implemented (yet):
 * publish, unpublish, lookup, spawn
 */

#endif /* _FLUX_CORE_PMI_SIMPLE_CLIENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
