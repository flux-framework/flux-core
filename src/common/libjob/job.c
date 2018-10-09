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
 *  any later version.
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "job.h"


flux_future_t *flux_job_submit (flux_t *h, const char *J, int priority,
                                int flags)
{
    flux_future_t *f;

    if (!h || !J) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h, "job-ingest.submit", FLUX_NODEID_ANY, 0,
                             "{s:s s:i s:i}",
                             "J", J,
                             "priority", priority,
                             "flags", flags)))
        return NULL;
    return f;
}

int flux_job_submit_get_id (flux_future_t *f, flux_jobid_t *jobid)
{
    flux_jobid_t id;

    if (!f || !jobid) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f, "{s:I}",
                                "id", &id) < 0)
        return -1;
    *jobid = id;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
