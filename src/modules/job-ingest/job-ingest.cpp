/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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

#include <flux/jobspec.hpp>

using namespace Flux::Jobspec;

extern "C" {
#include <flux/core.h>
#include "src/common/libutil/log.h"
}

extern "C" {
MOD_NAME ("job-ingest");
}

unsigned next_job_id;

static void job_submit_handler (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg);

static struct flux_msg_handler_spec msg_table[] = {
    { FLUX_MSGTYPE_REQUEST,  "job-ingest.submit", job_submit_handler, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

extern "C" int mod_main (flux_t *flux_handle, int argc, char **argv)
{
    if (flux_msg_handler_addvec (flux_handle, msg_table, NULL) < 0) {
        flux_log_error (flux_handle, "Failed to register message handlers");
        return -1;
    }

    flux_log (flux_handle, LOG_INFO, "before starting reactor");
    if (flux_reactor_run (flux_get_reactor (flux_handle), 0) < 0) {
        flux_log_error (flux_handle,
                        "The flux reactor exited with an unknown error");
    }
    flux_log (flux_handle, LOG_INFO, "after exiting reactor");

    flux_msg_handler_delvec (msg_table);

    return 0;
}

static void job_submit_handler (flux_t *handle, flux_msg_handler_t *msg_handler,
                                const flux_msg_t *request, void *arg)
{
    char *jobspec_encoded;
    int len;

    flux_log (handle, LOG_INFO, "Handling job-ingest.submitnew");
    flux_request_decode_raw (request, NULL, (void **)&jobspec_encoded, &len);

    /* FIXME: In the final design, "jobspec_encoded" will at this point be
       encoded by the security module.  Here we would optionally
       verify the signature (optionally, because it might be fine to
       allow a bad signature to be rejected later in the process by the
       IMP or flux shell if we think bad signatures are rare and it is
       worthwhile to skip that extra processsing step to speed up job
       ingest).  Next we would need to ask the security module to
       give us a decoded version of the jobspec. */

    std::string jobspec_raw;
    jobspec_raw = jobspec_encoded;

    // flux_log (handle, LOG_INFO, "Jobspec contents are:\n%s", jobspec_raw.c_str());
    /* Parse the jobspec to validate it's syntax */
    Jobspec js;
    try {
        js = Jobspec (jobspec_raw);
    } catch (...) {
        /* FIXME: provide error message needed */
        flux_log (handle, LOG_ERR, "Invalid jobspec");
        flux_respond (handle, request, 1, NULL);
        return;
    }

    /* FIXME: Here we might perform other sanity checks on the jobspec */

    char response[30];
    sprintf (response, "Your job's id is %u\n", next_job_id++);
    flux_respond_raw (handle, request, 0, response, strlen(response)+1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
