/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _JOB_HASH_H
#define _JOB_HASH_H

#include <flux/core.h>
#include <czmq.h>

/* Create a zhashx_t with hasher and comparator set to use flux_jobid_t
 * as the hash key.  The default key duplicator and destructor are disabled
 * on the presumption that the id is a member of the job object.
 *
 * Lookup:
 *   job = zhashx_lookup (hash, &jobid).
 * Insert:
 *   zhashx_insert (hash, &job->id, job)
 * Delete:
 *   zhashx_delete (hash, &jobid);
 */
zhashx_t *job_hash_create (void);

/* Optional: set a duplicator/destructor to have the hash manage
 * the life cycle of job objects, otherwise the life cycle
 * must be externally managed:
 *
 * void *job_duplicator (const void *item);
 * void job_destructor (void **item);
 *
 * zhashx_set_destructor (hash, job_destructor);
 * zhashx_set_duplicator (hash, job_duplicator);
 */

#endif /* _JOB_HASH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

