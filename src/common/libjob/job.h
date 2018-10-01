/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.  Additionally, the libflux-core library may be
 *  redistributed under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_CORE_JOB_H
#define _FLUX_CORE_JOB_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

enum job_submit_flags {
    FLUX_JOB_PRE_SIGNED = 1,    // 'jobspec' is already signed
};

enum job_priority {
    FLUX_JOB_PRIORITY_MIN = 0,
    FLUX_JOB_PRIORITY_DEFAULT = 16,
    FLUX_JOB_PRIORITY_MAX = 31,
};

typedef uint64_t flux_jobid_t;

/* Submit a job to the system.
 * 'jobspec' should be RFC 14 jobspec.
 * The system assigns a jobid and returns it in the response.
 */
flux_future_t *flux_job_submit (flux_t *h, const char *jobspec,
                                int priority, int flags);

/* Parse jobid from response to flux_job_submit() request.
 * Returns 0 on success, -1 on failure with errno set - and an extended
 * error message may be available with flux_future_error_string().
 */
int flux_job_submit_get_id (flux_future_t *f, flux_jobid_t *id);

/* Request a list of active jobs.
 * If 'max_entries' > 0, fetch at most that many jobs.
 * 'json_str' is an encoded JSON array of attribute strings, e.g.
 * ["id","userid",...] that will be returned in response.

 * Process the response payload with flux_rpc_get() or flux_rpc_get_unpack().
 * It is a JSON object containing an array of job objects, e.g.
 * { "jobs":[
 *   {"id":m, "userid":n},
 *   {"id":m, "userid":n},
 *   ...
 * ])
 */
flux_future_t *flux_job_list (flux_t *h, int max_entries,
                              const char *json_str);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
