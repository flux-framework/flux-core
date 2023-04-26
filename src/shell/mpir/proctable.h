/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>

#ifndef HAVE_PROCTABLE_H
#define HAVE_PROCTABLE_H

/* MPIR_PROCDESC is defined in the MPIR Process Acquisition Interface
 * Version 1:
 *
 * See https://www.mpi-forum.org/docs/mpir-specification-03-01-2018.pdf
 */
typedef struct {
    char *host_name;
    char *executable_name;
    int pid;
} MPIR_PROCDESC;


struct proctable * proctable_create (void);
void proctable_destroy (struct proctable *p);

/*  Convert a JSON-encoded proctable to struct proctable */
struct proctable * proctable_from_json (json_t *o);
struct proctable * proctable_from_json_string (const char *json_str);

/*  Encode a proctable into a jansson JSON object
 */
json_t * proctable_to_json (struct proctable *p);

/*  Append information for one task to the proctable `p`.
 */
int proctable_append_task (struct proctable *p,
                           const char *hostname,
                           const char *executable,
                           int taskid,
                           pid_t pid);

/*  Return the number of task entries in the proctable `p`
 */
int proctable_get_size (struct proctable *p);

/*  Return the taskid of the first task in proctable `p`
 *  (Useful for sorting proctables in a list)
 */
int proctable_first_task (const struct proctable *p);

int proctable_append_proctable_destroy (struct proctable *p1,
                                        struct proctable *p2);

/*  Create MPIR_proctable from proctable `p`. This table references
 *   memory from inside of `p`, so do not destroy `p` while the
 *   MPIR_proctable is in use. If `sizep` is not NULL, then the
 *   MPIR_proctable_size is returned in this variable.
 */
MPIR_PROCDESC *proctable_get_mpir_proctable (struct proctable *p,
                                             int *sizep);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
