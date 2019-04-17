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
#if WITH_TCMALLOC
#if HAVE_GPERFTOOLS_HEAP_PROFILER_H
  #include <gperftools/heap-profiler.h>
#elif HAVE_GOOGLE_HEAP_PROFILER_H
  #include <google/heap-profiler.h>
#else
  #error gperftools headers not configured
#endif
#endif /* WITH_TCMALLOC */

#include <flux/core.h>
#include "heaptrace.h"

static flux_msg_handler_t **handlers = NULL;

static void start_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    const char *filename;

    if (flux_request_unpack (msg, NULL, "{s:s}", "filename", &filename) < 0)
        goto error;
#if WITH_TCMALLOC
    if (IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto error;
    }
    HeapProfilerStart (filename);
#else
    errno = ENOSYS;
    goto error;
#endif
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static void dump_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    const char *reason;

    if (flux_request_unpack (msg, NULL, "{s:s}", "reason", &reason) < 0)
        goto error;
#if WITH_TCMALLOC
    if (!IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto error;
    }
    HeapProfilerDump (reason);
#else
    errno = ENOSYS;
    goto error;
#endif
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static void stop_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
#if WITH_TCMALLOC
    if (!IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto error;
    }
    HeapProfilerStop();
#else
    errno = ENOSYS;
    goto error;
#endif /* WITH_TCMALLOC */
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "heaptrace.start",  start_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "heaptrace.dump",   dump_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "heaptrace.stop",   stop_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static void heaptrace_finalize (void *arg)
{
    flux_msg_handler_delvec (handlers);
}

int heaptrace_initialize (flux_t *h)
{
    char *dummy = "hello";
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0)
        return -1;
    flux_aux_set (h, "flux::heaptrace", dummy, heaptrace_finalize);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
