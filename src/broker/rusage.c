/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <time.h>
#include <sys/resource.h>
#include <flux/core.h>
#include "rusage.h"

struct rusage_context {
    flux_msg_handler_t *mh;
};

static void rusage_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct rusage ru;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: flux_request_decode", __FUNCTION__);
        return;
    }
    if (getrusage (RUSAGE_THREAD, &ru) < 0)
        goto error;
    if (flux_respond_pack (h, msg,
            "{s:f s:f s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i s:i}",
            "utime", (double)ru.ru_utime.tv_sec + 1E-6 * ru.ru_utime.tv_usec,
            "stime", (double)ru.ru_stime.tv_sec + 1E-6 * ru.ru_stime.tv_usec,
            "maxrss", ru.ru_maxrss,
            "ixrss", ru.ru_ixrss,
            "idrss", ru.ru_idrss,
            "isrss", ru.ru_isrss,
            "minflt", ru.ru_minflt,
            "majflt", ru.ru_majflt,
            "nswap", ru.ru_nswap,
            "inblock", ru.ru_inblock,
            "oublock", ru.ru_oublock,
            "msgsnd", ru.ru_msgsnd,
            "msgrcv", ru.ru_msgrcv,
            "nsignals", ru.ru_nsignals,
            "nvcsw", ru.ru_nvcsw,
            "nivcsw", ru.ru_nivcsw) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void rusage_finalize (void *arg)
{
    struct rusage_context *r = arg;
    flux_msg_handler_stop (r->mh);
    flux_msg_handler_destroy (r->mh);
    free (r);
}

int rusage_initialize (flux_t *h, const char *service)
{
    struct flux_match match = FLUX_MATCH_ANY;
    struct rusage_context *r = calloc (1, sizeof (*r));
    if (!r) {
        errno = ENOMEM;
        goto error;
    }
    match.typemask = FLUX_MSGTYPE_REQUEST;
    if (asprintf (&match.topic_glob, "%s.rusage", service) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!(r->mh = flux_msg_handler_create (h, match, rusage_request_cb, r)))
        goto error;
    flux_msg_handler_start (r->mh);
    flux_aux_set (h, "flux::rusage", r, rusage_finalize);
    free (match.topic_glob);
    return 0;
error:
    if (r)
        rusage_finalize (r);
    free (match.topic_glob);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
