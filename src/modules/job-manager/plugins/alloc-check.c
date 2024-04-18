/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* alloc-check.c - plugin to ensure resources are never double booked
 *
 * A fatal exception is raised on jobs that are granted resources already
 * granted to another.
 *
 * In order to be sure that the exception can be raised before a short job
 * becomes inactive, R is looked up in the KVS synchronously, causing the
 * job manager to be briefly unresponsive.  Hence, this plugin is primarily
 * suited for debug/test situations.
 *
 * N.B.  This plugin does not account for any jobs that might already have
 * allocations when the plugin is loaded.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "ccan/str/str.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libeventlog/eventlog.h"

#define PLUGIN_NAME    "alloc-check"
static const char *auxname = PLUGIN_NAME "::resdb";

/* Start out with empty resource set.  Add resources on job.event.alloc
 * (scheduler has allocated resources to job).  Subtract resources on
 * job.event.free (job manager has returned resources to the scheduler).
 */
struct resdb {
    struct rlist *allocated;
};

static void resdb_destroy (struct resdb *resdb)
{
    if (resdb) {
        int saved_errno = errno;
        rlist_destroy (resdb->allocated);
        free (resdb);
        errno = saved_errno;
    }
}

static struct resdb *resdb_create (void)
{
    struct resdb *resdb;

    if (!(resdb = calloc (1, sizeof (*resdb))))
        return NULL;
    if (!(resdb->allocated = rlist_create())) {
        free (resdb);
        errno = ENOMEM;
        return NULL;
    }
    return resdb;
}

/* Generate the kvs path to R for a given job
 */
static int res_makekey (flux_jobid_t id, char *buf, size_t size)
{
    char dir[128];
    if (flux_job_id_encode (id, "kvs", dir, sizeof (dir)) < 0)
        return -1;
    if (snprintf (buf, size, "%s.R", dir) >= size) {
        errno = EOVERFLOW;
        return -1;
    }
    return 0;
}

/* Synchronously look up R for a given job and convert it to an rlist object
 * which the caller must destroy with rlist_destroy().
 */
static struct rlist *res_lookup (flux_t *h, flux_jobid_t id)
{
    char key[128];
    flux_future_t *f = NULL;
    const char *R;
    struct rlist *rlist;

    if (res_makekey (id, key, sizeof (key)) < 0
        || !(f = flux_kvs_lookup (h, NULL, 0, key))
        || flux_kvs_lookup_get (f, &R) < 0
        || !(rlist = rlist_from_R (R))) {
        flux_future_destroy (f);
        return NULL;
    }
    flux_future_destroy (f);
    return rlist;
}

/* When a job is presented to the scheduler via the RFC 27 'hello' handshake
 * upon scheduler reload, the scheduler raises a fatal scheduler-restart
 * exception if it cannot re-allocate the job's resources and the job manager
 * marks resources free without posting a free event.  This plugin must
 * account for those resources.  See flux-framework/flux-core#5889
 */
static bool is_hello_failure (json_t *entry)
{
    const char *type;
    int severity;
    json_t *context;

    if (eventlog_entry_parse (entry, NULL, NULL, &context) == 0
        && json_unpack (context,
                        "{s:i s:s}",
                        "severity", &severity,
                        "type", &type) == 0
        && severity == 0
        && streq (type, "scheduler-restart"))
        return true;
    return false;
}

static int jobtap_cb (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *arg)
{
    struct resdb *resdb = flux_plugin_aux_get (p, auxname);
    flux_t *h = flux_jobtap_get_flux (p);
    flux_jobid_t id;
    json_t *entry = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s?o}",
                                "id", &id,
                                "entry", &entry) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "%s %s: unpack: %s",
                  PLUGIN_NAME,
                  topic,
                  flux_plugin_arg_strerror (args));
        return -1;
    }
    /* job.event.* callbacks are not received unless subscribed on a per-job
     * basis, so subscribe to them in the job.new callback.
     */
    if (streq (topic, "job.new")) {
        if (flux_jobtap_job_subscribe (p, id) < 0) {
            flux_log_error (h,
                            "%s(%s) %s: subscribe",
                            PLUGIN_NAME,
                            idf58 (id),
                            topic);
        }
    }
    /* Look up R that was just allocated to the job and attach it to the job
     * aux container so we don't have to look it up again on free.  Call
     * rlist_append() to add the resources to resdb->allocated.  If that
     * fails, some resources are already allocated so raise a fatal exception
     * on the job.
     */
    else if (streq (topic, "job.event.alloc")) {
        struct rlist *R;
        if (!(R = res_lookup (h, id))
            || flux_jobtap_job_aux_set (p,
                                        id,
                                        PLUGIN_NAME "::R",
                                        R,
                                        (flux_free_f)rlist_destroy) < 0) {
            flux_log_error (h,
                            "%s(%s) %s: failed to lookup or cache R",
                            PLUGIN_NAME,
                            idf58 (id),
                            topic);
            rlist_destroy (R);
            return -1;
        }
        if (rlist_append (resdb->allocated, R) < 0) {
            flux_jobtap_raise_exception (p,
                                         id,
                                         "alloc-check",
                                         0,
                                         "resources already allocated");
        }
    }
    /* Get R that was just freed from the job's aux container and remove it
     * from resdb->allocated.  Any jobs that had allocations before the module
     * will not have the R aux item, so silently return success in that case.
     */
    else if (streq (topic, "job.event.free")
        || (streq (topic, "job.event.exception") && is_hello_failure (entry))) {
        struct rlist *R = flux_jobtap_job_aux_get (p, id, PLUGIN_NAME "::R");
        if (R) {
            struct rlist *diff;
            if (!(diff = rlist_diff (resdb->allocated, R))) {
                flux_log_error (h,
                                "%s(%s) %s: rlist_diff",
                                PLUGIN_NAME,
                                idf58 (id),
                                topic);
                return -1;
            }
            rlist_destroy (resdb->allocated);
            resdb->allocated = diff;
        }
    }
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.event.alloc",   jobtap_cb,    NULL },
    { "job.event.free",    jobtap_cb,    NULL },
    { "job.event.exception", jobtap_cb,  NULL },
    { "job.new",           jobtap_cb,    NULL },
    { 0 }
};

int flux_plugin_init (flux_plugin_t *p)
{
    struct resdb *resdb;

    if (!(resdb = resdb_create ())
        || flux_plugin_aux_set (p,
                                auxname,
                                resdb,
                                (flux_free_f)resdb_destroy) < 0) {
        resdb_destroy (resdb);
        return -1;
    }
    return flux_plugin_register (p, "alloc-check", tab);
}

// vi:ts=4 sw=4 expandtab
