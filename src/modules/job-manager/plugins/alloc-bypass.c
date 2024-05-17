/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* alloc-bypass.c - If attributes.system.R exists in jobspec, then
 *  bypass scheduler alloc protocol and use R directly (for instance
 *  owner use only)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/types.h>

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/librlist/rlist.h"
#include "src/common/libjob/idf58.h"

static void alloc_continuation (flux_future_t *f, void *arg)
{
    flux_plugin_t *p = arg;
    flux_jobid_t *idptr = flux_future_aux_get (f, "jobid");

    if (flux_future_get (f, NULL) < 0) {
        flux_jobtap_raise_exception (p,
                                     *idptr,
                                     "alloc", 0,
                                     "failed to commit R to kvs: %s",
                                      strerror (errno));
        goto done;
    }
    if (flux_jobtap_event_post_pack (p,
                                     *idptr,
                                     "alloc",
                                     "{s:b}",
                                     "bypass", true) < 0) {
        flux_jobtap_raise_exception (p,
                                     *idptr,
                                     "alloc", 0,
                                     "failed to post alloc event: %s",
                                     strerror (errno));
        goto done;
    }
done:
    flux_future_destroy (f);
}

static flux_future_t *commit_R (flux_plugin_t *p,
                                flux_jobid_t id,
                                json_t *R)
{
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_t *h = flux_jobtap_get_flux (p);
    char key[64];

    if (!h
        || flux_job_kvs_key (key, sizeof (key), id, "R") < 0
        || !(txn = flux_kvs_txn_create ()))
        return NULL;

    if (flux_kvs_txn_pack (txn, 0, key, "O", R) < 0)
        goto out;

    f = flux_kvs_commit (h, NULL, 0, txn);
out:
    flux_kvs_txn_destroy (txn);
    return f;
}

static int alloc_start (flux_plugin_t *p,
                        flux_jobid_t id,
                        json_t *R)
{
    int saved_errno;
    flux_future_t *f = NULL;
    flux_jobid_t *idptr = NULL;

    if (!(f = commit_R (p, id, R))
        || flux_future_then (f, -1, alloc_continuation, p) < 0)
        goto error;

    if (!(idptr = malloc (sizeof (*idptr)))
        || flux_future_aux_set (f, "jobid", idptr, free) < 0)
        goto error;

    *idptr = id;
    return 0;
error:
    saved_errno = errno;
    flux_future_destroy (f);
    free (idptr);
    errno = saved_errno;
    return -1;
}

static int sched_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    json_t *R;
    flux_jobid_t id;

    /*  If alloc-bypass::R set on this job then commit R to KVS
     *   and set alloc-bypass flag
     */
    if (!(R = flux_jobtap_job_aux_get (p,
                                       FLUX_JOBTAP_CURRENT_JOB,
                                       "alloc-bypass::R")))
        return 0;


    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I}",
                                "id", &id) < 0) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "alloc", 0,
                                     "alloc-bypass: %s: unpack: %s",
                                     topic,
                                     flux_plugin_arg_strerror (args));
        return -1;
    }

    if (alloc_start (p, id, R) < 0)
        flux_jobtap_raise_exception (p, id, "alloc", 0,
                                     "failed to commit R to kvs");
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "{s:O}", "R", R) < 0)
        return -1;

    return 0;
}

static int validate_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *arg)
{
    json_t *R = NULL;
    struct rlist *rl;
    json_error_t error;
    uint32_t userid = (uint32_t) -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i s:{s:{s?{s?{s?o}}}}}",
                                "userid", &userid,
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "alloc-bypass",
                                    "R", &R) < 0) {
        return flux_jobtap_reject_job (p,
                                       args,
                                       "invalid system.alloc-bypass.R: %s",
                                       flux_plugin_arg_strerror (args));
    }

    /*  Nothing to do if no R provided
     */
    if (R == NULL)
        return 0;

    if (userid != getuid ())
        return flux_jobtap_reject_job (p,
                                       args,
                                       "Guest user cannot use alloc bypass");

    /*  Sanity check R for validity
     */
    if (!(rl = rlist_from_json (R, &error)))
        return flux_jobtap_reject_job (p,
                                       args,
                                       "alloc-bypass: invalid R: %s",
                                       error.text);
    rlist_destroy (rl);

    /*  Store R in job structure to avoid re-fetching from plugin args
     *   in job.state.sched callback.
     */
    if (flux_jobtap_job_aux_set (p,
                                 FLUX_JOBTAP_CURRENT_JOB,
                                 "alloc-bypass::R",
                                 json_incref (R),
                                 (flux_free_f)json_decref) < 0) {
        int saved_errno = errno;
        json_decref (R);
        return flux_jobtap_reject_job (p,
                                       args,
                                       "failed to capture alloc-bypass R: %s",
                                       strerror (saved_errno));
    }

    if (flux_jobtap_job_set_flag (p,
                                  FLUX_JOBTAP_CURRENT_JOB,
                                  "alloc-bypass") < 0) {
        flux_jobtap_raise_exception (p, FLUX_JOBTAP_CURRENT_JOB,
                                     "alloc", 0,
                                     "Failed to set alloc-bypass: %s",
                                     strerror (errno));
        return -1;
    }

    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.state.sched",   sched_cb,    NULL },
    { "job.validate",      validate_cb, NULL },
    { 0 }
};


int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_register (p, "alloc-bypass", tab);
}

// vi:ts=4 sw=4 expandtab
