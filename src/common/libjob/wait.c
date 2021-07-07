/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <jansson.h>
#include <flux/core.h>

#include "job.h"

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errno_safe.h"

struct wait_result {
    bool success;
    char errbuf[128];
};

static int decode_job_result (json_t *event, struct wait_result *result)
{
    const char *name;
    json_t *context;

    if (eventlog_entry_parse (event, NULL, &name, &context) < 0)
        return -1;

    /* Exception - set errbuf=description, set success=false
     */
    if (!strcmp (name, "exception")) {
        const char *type;
        const char *note = NULL;

        if (json_unpack (context,
                         "{s:s s?:s}",
                         "type",
                         &type,
                         "note",
                         &note) < 0)
            return -1;
        (void)snprintf (result->errbuf,
                        sizeof (result->errbuf),
                        "Fatal exception type=%s %s",
                        type,
                        note ? note : "");
        result->success = false;
    }
    /* Shells exited - set errbuf=decoded status byte,
     * set success=true if all shells exited with 0, otherwise false.
     */
    else if (!strcmp (name, "finish")) {
        int status;

        if (json_unpack (context, "{s:i}", "status", &status) < 0)
            return -1;
        if (WIFSIGNALED (status)) {
            (void)snprintf (result->errbuf,
                            sizeof (result->errbuf),
                            "task(s) %s",
                            strsignal (WTERMSIG (status)));
            result->success = false;
        }
        else if (WIFEXITED (status)) {
            (void)snprintf (result->errbuf,
                            sizeof (result->errbuf),
                            "task(s) exited with exit code %d",
                            WEXITSTATUS (status));
            result->success = WEXITSTATUS (status) == 0 ? true : false;
        }
        else {
            (void)snprintf (result->errbuf,
                            sizeof (result->errbuf),
                            "unexpected wait(2) status %d",
                            status);
            result->success = false;
        }
    }
    else
        return -1;
    return 0;
}

flux_future_t *flux_job_wait (flux_t *h, flux_jobid_t id)
{
    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    return flux_rpc_pack (h,
                          "job-manager.wait",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:I}",
                          "id",
                          id);
}

int flux_job_wait_get_status (flux_future_t *f,
                              bool *successp,
                              const char **errstrp)
{
    const char *auxkey = "flux::wait_result";
    struct wait_result *result;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (!(result = flux_future_aux_get (f, auxkey))) {
        json_t *event;

        if (flux_rpc_get_unpack (f,
                                 "{s:o}",
                                 "event",
                                 &event) < 0)
            return -1;
        if (!(result = calloc (1, sizeof (*result))))
            return -1;
        if (decode_job_result (event, result) < 0) {
            free (result);
            errno = EPROTO;
            return -1;
        }
        if (flux_future_aux_set (f, auxkey, result, free) < 0) {
            ERRNO_SAFE_WRAP (free, result);
            return -1;
        }
    }
    if (successp)
        *successp = result->success;
    if (errstrp)
        *errstrp = result->errbuf;
    return 0;
}

int flux_job_wait_get_id (flux_future_t *f, flux_jobid_t *jobid)
{
    flux_jobid_t id;

    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (flux_rpc_get_unpack (f, "{s:I}",
                                "id", &id) < 0)
        return -1;
    if (jobid)
        *jobid = id;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
