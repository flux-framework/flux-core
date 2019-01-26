/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* validate - asynchronous jobspec validation interface
 *
 * Spawn worker(s) to validate jobspec.  Up to 'DEFAULT_WORKER_COUNT'
 * workers may be active at one time.  They are started lazily, on demand,
 * and stop after a period of inactivity (see "tunables" below).
 *
 * The validator executable and its command line, including the
 * location of jobspec.jsonschema, are currently hardwired.
 *
 * Jobspec is expected to be in encoded JSON form, with or without
 * whitespace or NULL termination.  The encoding is normalized before
 * it is sent to the worker on a single line.
 *
 * The future is fulfilled with the result of validation.  On success,
 * the container will be empty.  On failure, the reason the jobspec
 * did not pass validation (suitable for returning to the submitting user)
 * will be assigned to the future's extended error string.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "validate.h"
#include "worker.h"

/* Tunables:
 */

/* The maximum number of concurrent workers.
 */
#define MAX_WORKER_COUNT 4

/* Start a new worker if backlog reaches this level for all active workers.
 */
const int worker_queue_threshold = 32;

/* Workers exit once they have been inactive for this many seconds.
 */
const double worker_inactivity_timeout = 5.0;



struct validate {
    flux_t *h;
    struct worker *worker[MAX_WORKER_COUNT];
};

void validate_destroy (struct validate *v)
{
    if (v) {
        int saved_errno = errno;
        int i;
        for (i = 0; i < MAX_WORKER_COUNT; i++)
            worker_destroy (v->worker[i]);
        free (v);
        errno = saved_errno;
    }
}

struct validate *validate_create (flux_t *h)
{
    struct validate *v;
    char *argv[4];
    int conf_flags = 0;
    int i;

    if (!(v = calloc (1, sizeof (*v))))
        return NULL;
    v->h = h;

    if (getenv ("FLUX_CONF_INTREE"))
        conf_flags |= CONF_FLAG_INTREE;
    argv[0] = (char *)flux_conf_get ("jobspec_validate_path", conf_flags);
    argv[1] = "--schema";
    argv[2] = (char *)flux_conf_get ("jobspec_schema_path", conf_flags);
    argv[3] = NULL;
    assert (argv[0] != NULL);
    assert (argv[2] != NULL);
    for (i = 0; i < MAX_WORKER_COUNT; i++) {
        if (!(v->worker[i] = worker_create (h, worker_inactivity_timeout,
                                            3, argv)))
            goto error;
    }
    return v;
error:
    validate_destroy (v);
    return NULL;
}

/* Select worker with least backlog.  If none is running, or the best
 * has a backlog at or beyond threshold, activate a new one, if possible.
 */
struct worker *select_best_worker (struct validate *v)
{
    struct worker *best = NULL;
    struct worker *idle = NULL;
    int i;

    for (i = 0; i < MAX_WORKER_COUNT; i++) {
        if (worker_is_running (v->worker[i])) {
            if (!best || (worker_queue_depth (v->worker[i])
                        < worker_queue_depth (best)))
                best = v->worker[i];
        }
        else if (!idle)
            idle = v->worker[i];
    }
    if (idle && (!best || worker_queue_depth (best) >= worker_queue_threshold))
        best = idle;

    return best;
}

flux_future_t *validate_jobspec (struct validate *v, const char *buf, int len)
{
    flux_future_t *f;
    json_t *o;
    json_error_t error;
    char *s;
    int saved_errno;
    struct worker *w;

    /* Make sure jobspec decodes as JSON (no YAML allowed here).
     * Capture any JSON parsing errors by returning them in a future.
     * Then re-encode in compact form to eliminate any white space (esp \n).
     */
    if (!(o = json_loadb (buf, len, 0, &error))) {
        char errbuf[256];
        if (!(f = flux_future_create (NULL, NULL)))
            return NULL;
        flux_future_set_flux (f, v->h);
        (void)snprintf (errbuf, sizeof (errbuf),
                       "jobspec: invalid JSON: %s", error.text);
        flux_future_fulfill_error (f, EINVAL, errbuf);
        return f;
    }
    if (!(s = json_dumps (o, JSON_COMPACT)))
        goto error;
    w = select_best_worker (v);
    assert (w != NULL);
    if (!(f = worker_request (w, s)))
        goto error;
    free (s);
    json_decref (o);
    return f;
error:
    saved_errno = errno;
    free (s);
    json_decref (o);
    errno = saved_errno;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
