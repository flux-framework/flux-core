/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_PMI_SIMPLE_SERVER_H
#define _FLUX_CORE_PMI_SIMPLE_SERVER_H

struct pmi_simple_server;

#define SIMPLE_KVS_KEY_MAX         64
#define SIMPLE_KVS_VAL_MAX         1024
#define SIMPLE_KVS_NAME_MAX        64

#define SIMPLE_MAX_PROTO_OVERHEAD  64

#define SIMPLE_MAX_PROTO_LINE \
    (SIMPLE_KVS_KEY_MAX + SIMPLE_KVS_VAL_MAX \
                        + SIMPLE_KVS_NAME_MAX \
                        + SIMPLE_MAX_PROTO_OVERHEAD)


/* User-provided service implementation.
 * Integer return: 0 on success, -1 on failure.
 */
struct pmi_simple_ops {
    int (*kvs_put)(void *arg, const char *kvsname,
                   const char *key, const char *val);
    int (*kvs_get)(void *arg, void *cli, const char *kvsname, const char *key);
    int (*barrier_enter)(void *arg);
    int (*response_send)(void *client, const char *buf);
    void (*debug_trace)(void *client, const char *buf);
};

enum {
    PMI_SIMPLE_SERVER_TRACE = 1,
};

/* Create/destroy protocol engine.
 */
struct pmi_simple_server *pmi_simple_server_create (struct pmi_simple_ops ops,
                                                    int appnum,
                                                    int universe_size,
                                                    int local_size,
                                                    const char *kvsname,
                                                    int flags,
                                                    void *arg);
void pmi_simple_server_destroy (struct pmi_simple_server *pmi);

/* Put null-terminated request with sending client reference to protocol
 * engine.  The request should end with a newline.
 * Returns 1 indicating finalized / close fd, 0 on success, -1 on failure.
 */
int pmi_simple_server_request (struct pmi_simple_server *pmi,
                               const char *buf, void *client, int rank);

/* Finalize a barrier.  Set rc to 0 for success, -1 for failure.
 */
int pmi_simple_server_barrier_complete (struct pmi_simple_server *pmi, int rc);

/* Finalize a kvs_get.
 */
int pmi_simple_server_kvs_get_complete (struct pmi_simple_server *pmi,
                                        void *client, const char *val);

#endif /* ! _FLUX_CORE_PMI_SIMPLE_SERVER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
