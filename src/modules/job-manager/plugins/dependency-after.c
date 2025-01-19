/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dependency-simple.c - don't start a job until after another starts,
 *   completes, or fails.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libutil/iterators.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

static zlistx_t *global_reflist = NULL;

/* Types of "after*" dependencies:
 */
enum after_type {
    AFTER_START =   0x1,
    AFTER_FINISH =  0x2,
    AFTER_SUCCESS = 0x4,
    AFTER_FAILURE = 0x8
};

struct after_info {
    enum after_type type;
    flux_jobid_t depid;
    char *description;
};

/*  Reference to an after_info object on another job's dependency list
 */
struct after_ref {
    flux_jobid_t id;
    zlistx_t *list;
    struct after_info *info;
};

static const char * after_typestr (enum after_type type)
{
    switch (type) {
        case AFTER_START:
            return "after-start";
        case AFTER_FINISH:
            return "after-finish";
        case AFTER_SUCCESS:
            return "after-success";
        case AFTER_FAILURE:
            return "after-failure";
    }
    return "";
}

static int after_type_parse (const char *s, enum after_type *tp)
{
    if (streq (s, "after"))
        *tp = AFTER_START;
    else if (streq (s, "afterany"))
        *tp = AFTER_FINISH;
    else if (streq (s, "afterok"))
        *tp = AFTER_SUCCESS;
    else if (streq (s, "afternotok"))
        *tp = AFTER_FAILURE;
    else
        return -1;
    return 0;
}

static void after_info_destroy (struct after_info *after)
{
    if (after) {
        free (after->description);
        free (after);
    }
}

/*  zlistx_destructor_fn for after_info objects
 */
static void after_info_destructor (void **item)
{
    if (*item) {
        after_info_destroy (*item);
        *item = NULL;
    }
}

static struct after_info *after_info_create (flux_jobid_t id,
                                             enum after_type type,
                                             const char *desc)
{
    struct after_info *after = calloc (1, sizeof (*after));
    if (!after
        || asprintf (&after->description,
                     "%s=%s",
                     after_typestr (type),
                     desc) < 0)
        goto error;
    after->type = type;
    after->depid = id;
    return after;
error:
    after_info_destroy (after);
    return NULL;
}

static void after_ref_destroy (struct after_ref *ref)
{
    free (ref);
}

/*  zlistx_destructor_fn for after_ref objects
 */
static void after_ref_destructor (void **item)
{
    if (*item) {
        void *handle = zlistx_find (global_reflist, *item);
        if (handle)
            zlistx_delete (global_reflist, handle);
        after_ref_destroy (*item);
        *item = NULL;
    }
}

static struct after_ref * after_ref_create (flux_jobid_t id,
                                            zlistx_t *l,
                                            struct after_info *after)
{
    struct after_ref *ref = calloc (1, sizeof (*ref));
    if (!ref)
        return NULL;
    ref->id = id;
    ref->list = l;
    ref->info = after;
    zlistx_add_end (global_reflist, ref);
    return ref;
}

/*  flux_free_f destructor for zlistx_t
 */
static void list_destructor (void *arg)
{
    zlistx_t *l = arg;
    zlistx_destroy (&l);
}

/*  Get or create a list embedded in jobid id
 */
static zlistx_t * embedded_list_get (flux_plugin_t *p,
                                     flux_jobid_t id,
                                     const char *name,
                                     zlistx_destructor_fn destructor)
{
    zlistx_t *l = flux_jobtap_job_aux_get (p, id, name);
    if (!l) {
        if (!(l = zlistx_new ())) {
            errno = ENOMEM;
            return NULL;
        }
        if (flux_jobtap_job_aux_set (p, id, name, l, list_destructor) < 0) {
            zlistx_destroy (&l);
            errno = ENOENT;
            return NULL;
        }
        if (destructor)
            zlistx_set_destructor (l, destructor);
    }
    return l;
}

static zlistx_t * after_list_get (flux_plugin_t *p, flux_jobid_t id)
{
    return embedded_list_get (p, id,
                              "flux::after_list",
                              (zlistx_destructor_fn *) after_info_destructor);
}

static zlistx_t * after_refs_get (flux_plugin_t *p, flux_jobid_t id)
{
    return embedded_list_get (p, id,
                              "flux::after_refs",
                              (zlistx_destructor_fn *) after_ref_destructor);
}

static zlistx_t *after_list_check (flux_plugin_t *p, flux_jobid_t id)
{
    return flux_jobtap_job_aux_get (p,
                                    id > 0 ? id : FLUX_JOBTAP_CURRENT_JOB,
                                    "flux::after_list");
}

static zlistx_t *after_refs_check (flux_plugin_t *p)
{
    return flux_jobtap_job_aux_get (p,
                                    FLUX_JOBTAP_CURRENT_JOB,
                                    "flux::after_refs");
}

/*  Lookup a job and return its userid and state information.
 *
 *   `id` may  be FLUX_JOBTAP_CURRENT_JOB to return information for
 *   the current jobtap jobid.
 */
static int lookup_job_uid_state (flux_plugin_t *p,
                                 flux_jobid_t id,
                                 uint32_t *puid,
                                 flux_job_state_t *pstate)
{
    int rc = 0;
    flux_plugin_arg_t *args = flux_jobtap_job_lookup (p, id);
    if (!args
        || flux_plugin_arg_unpack (args,
                                   FLUX_PLUGIN_ARG_IN,
                                   "{s:i s:i}",
                                   "userid", puid,
                                   "state", pstate) < 0)
        rc = -1;
    flux_plugin_arg_destroy (args);
    return rc;
}

/*  Handle a job in INACTIVE state.
 *
 *  Get the job result and check for various error states (e.g. afterok
 *   and job already completed with failure, after and job had an exception
 *   before it was started, etc.)
 */
static int dependency_handle_inactive (flux_plugin_t *p,
                                       flux_plugin_arg_t *args,
                                       struct after_info *after,
                                       flux_jobid_t afterid,
                                       const char *jobid)
{
    int rc = -1;
    flux_job_result_t result;
    enum after_type type = after->type;

    if (flux_jobtap_get_job_result (p, afterid, &result) < 0)
        return flux_jobtap_reject_job (p, args,
                                       "dependency: failed to get %ss result",
                                       jobid);

    /*  If the target job did not enter RUN state, immediately raise an
     *  exception since after* dependencies only apply to jobs that actually
     *  ran:
     */
    if (!flux_jobtap_job_event_posted (p, afterid, "alloc"))
        return flux_jobtap_reject_job (p,
                                       args,
                                       "dependency: after: %s never started",
                                       jobid);
    if (type == AFTER_START
        && !flux_jobtap_job_event_posted (p, afterid, "start"))
        return flux_jobtap_reject_job (p,
                                       args,
                                       "dependency: after: %s never started",
                                       jobid);
    if (type == AFTER_SUCCESS
        && result != FLUX_JOB_RESULT_COMPLETED)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "dependency: afterok: "
                                       "job %s failed or was canceled",
                                       jobid);
    if (type == AFTER_FAILURE
        && result == FLUX_JOB_RESULT_COMPLETED)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "dependency: afternotok:"
                                       " job %s succeeded",
                                        jobid);
    rc = flux_jobtap_dependency_remove (p, after->depid, after->description);
    if (rc < 0)
        flux_log_error (flux_jobtap_get_flux (p),
                        "flux_jobtap_dependency_remove");
    return rc;
}

/*  Handler for job.dependency.after*
 *
 */
static int dependency_after_cb (flux_plugin_t *p,
                                const char *topic,
                                flux_plugin_arg_t *args,
                                void *data)
{
    const char *scheme = NULL;
    const char *jobid = NULL;
    enum after_type type;
    flux_jobid_t afterid;
    flux_jobid_t id;
    uint32_t uid;
    uint32_t target_uid;
    flux_job_state_t target_state;
    struct after_info *after;
    struct after_ref *ref;
    zlistx_t *l;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i s:{s:s s:s}}",
                                "id", &id,
                                "userid", &uid,
                                "dependency",
                                "scheme", &scheme,
                                "value", &jobid) < 0)
        return flux_jobtap_reject_job (p, args,
                                       "dependency: after: %s",
                                       flux_plugin_arg_strerror (args));

    /*  Parse the type of dependency being requested from the scheme:
     */
    if (after_type_parse (scheme, &type) < 0)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "invalid dependency scheme: %s",
                                       scheme);

    /*  Parse the value argument, which must be a valid jobid
     *  Do not allow FLUX_JOBID_ANY/FLUX_JOBTAP_CURRENT_JOBID to be specified
     */
    if (flux_job_id_parse (jobid, &afterid) < 0
        || afterid == FLUX_JOBTAP_CURRENT_JOB)
        return flux_jobtap_reject_job (p, args,
                                       "%s: %s: \"%s\" is not a valid jobid",
                                       "dependency",
                                       scheme,
                                       jobid);

    /*  Lookup userid and state of target job `afterid`
     */
    if (lookup_job_uid_state (p, afterid, &target_uid, &target_state) < 0) {
        return flux_jobtap_reject_job (p, args,
                                       "%s: %s: id %s: %s",
                                       "dependency",
                                       scheme,
                                       jobid,
                                       errno == ENOENT ?
                                       "job not found" :
                                       strerror (errno));
    }

    /*  Requesting userid must match target job uid
     */
    if (uid != target_uid)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "%s: Permission denied for job %s",
                                       scheme,
                                       jobid);

    if (!(after = after_info_create (id, type, jobid)))
        return flux_jobtap_reject_job (p, args,
                                       "failed to establish job dependency");

    /*  Emit the dependency
     */
    if (flux_jobtap_dependency_add (p, id, after->description) < 0) {
        after_info_destroy (after);
        return flux_jobtap_reject_job (p, args, "Unable to add job dependency");
    }

    /*  If the job is already INACTIVE, then the dependency can be resolved
     *   immediately, or if the dependency cannot be resolved then the job
     *   is rejected.
     */
    if (target_state == FLUX_JOB_STATE_INACTIVE) {
        int rc = dependency_handle_inactive (p, args, after, afterid, jobid);
        after_info_destroy (after);
        return rc;
    }

    /*  Corner case, requisite job may have already started. Check for that
     *   here and immediately satisfy dependency before adding to various
     *   lists below
     */
    if (type == AFTER_START
        && flux_jobtap_job_event_posted (p, afterid, "start") == 1) {
        if (flux_jobtap_dependency_remove (p, id, after->description) < 0)
            flux_log_error (flux_jobtap_get_flux (p),
                            "flux_jobtap_dependency_remove");
        after_info_destroy (after);
        return 0;
    }

    /*  Append this dependency to the deplist in the target jobid:
     */
    if (!(l = after_list_get (p, afterid))
        || !zlistx_add_end (l, after)) {
        after_info_destroy (after);
        return flux_jobtap_reject_job (p,
                                       args,
                                       "failed to append to list");
    }

    /*  Create a reference in the current job to the depednency, so it can
     *   be removed if this job terminates before PRIORITY state.
     */
    if (!(ref = after_ref_create (afterid, l, after))
        || !(l = after_refs_get (p, id))
        || !zlistx_add_end (l, ref)) {
        after_info_destroy (after);
        after_ref_destroy (ref);
        return flux_jobtap_reject_job (p, args, "failed to create ref");
    }

    /*  If the target is AFTER_START, subscribe to events for the target
     *   jobid so this plugin gets the job.event.start callback for the
     *   target job.
     */
    if (type == AFTER_START
        && flux_jobtap_job_subscribe (p, afterid) < 0) {
        after_info_destroy (after);
        after_ref_destroy (ref);
        return flux_jobtap_reject_job (p, args, "failed to subscribe to %s",
                                       idf58 (id));
    }

    return 0;
}


/*  Attempt to remove the job dependency described by `after`. If this
 *   fails, try to raise a fatal job exception on the current job.
 */
static void remove_jobid_dependency (flux_plugin_t *p,
                                     struct after_info *after)
{
    if (flux_jobtap_dependency_remove (p,
                                       after->depid,
                                       after->description) < 0) {
        if (flux_jobtap_raise_exception (p,
                                         after->depid,
                                         "dependency",
                                         0,
                                         "Failed to remove dependency %s",
                                         after->description) < 0) {
            flux_log_error (flux_jobtap_get_flux (p),
                            "flux_jobtap_raise_exception: id=%s",
                            idf58 (after->depid));
        }
    }
}


/*  Release all dependent jobs in the dependency list `l` with types
 *   in the mask `typemask`.
 */
static void release_all (flux_plugin_t *p, zlistx_t *l, int typemask)
{
    if (l) {
        struct after_info *after = zlistx_first (l);
        while (after) {
            if (after->type & typemask) {
                /*  Remove dependency (possibly moving dependent job
                 *   out of the DEPEND state.
                 */
                remove_jobid_dependency (p, after);

                /*  Delete this entry since it has been resolved.
                 */
                if (zlistx_delete (l, zlistx_cursor (l)) < 0)
                    flux_log (flux_jobtap_get_flux (p),
                              LOG_ERR,
                              "release_all: zlistx_delete");
            }
            after = zlistx_next (l);
        }
    }
}

/*
 *  Raise exceptions for all unhandled depednencies in list `l`.
 */
static void raise_exceptions (flux_plugin_t *p, zlistx_t *l, const char *msg)
{
    if (l) {
        struct after_info *after;
        if (!msg)
            msg = "can never be satisfied";
        FOREACH_ZLISTX (l, after) {
            if (flux_jobtap_raise_exception (p,
                                             after->depid,
                                             "dependency",
                                             0,
                                             "dependency %s %s",
                                             after->description,
                                             msg) < 0)
                flux_log_error (flux_jobtap_get_flux (p),
                                "id=%s: unable to raise exception for %s",
                                idf58 (after->depid),
                                after->description);
        }
        /*  N.B. = entry will be deleted at list destruction */
    }
}

/*  If this job has any outstanding after-dependency references, then the
 *   job has transitioned from dependency state directly to cleanup
 *   (e.g. due to cancelation) and the refs must be cleaned up. This avoids
 *   prerequisite jobs from emitting erroneous dependencies for completed
 *   jobs.
 */
static void release_dependency_references (flux_plugin_t *p)
{
    flux_t *h = flux_jobtap_get_flux (p);
    zlistx_t *l;
    if ((l = after_refs_check (p))) {
        struct after_ref *ref = zlistx_first (l);
        while (ref) {
            /*  For each after_ref entry, check to ensure the job and its
             *   after dependencies list still exists. If so, remove this
             *   job's entry from the list.
             */
            zlistx_t *after_list = after_list_check (p, ref->id);
            if (after_list == ref->list) {
                void *handle = zlistx_find (after_list, ref->info);
                if (handle && zlistx_delete (ref->list, handle) < 0) {
                    flux_log_error (h, "%s: %s: zlistx_delete",
                                    "dependency-after",
                                    "release_references");
                }
            }
            ref = zlistx_next (l);
        }
    }

    /*  Destroy this job's dependency reference list.
     */
    if (flux_jobtap_job_aux_delete (p, FLUX_JOBTAP_CURRENT_JOB, l) < 0)
        flux_log_error (h, "release_references: flux_jobtap_job_aux_delete");
}

static int release_dependent_jobs (flux_plugin_t *p, zlistx_t *l)
{
    flux_t *h = flux_jobtap_get_flux (p);
    flux_job_result_t result;

    if (l == NULL)
        return 0;

    if (flux_jobtap_get_job_result (p,
                                    FLUX_JOBTAP_CURRENT_JOB,
                                    &result) < 0) {
        flux_log_error (h, "dependency-after: flux_jobtap_get_result");
        return -1;
    }

    /*  If this job never entered RUN state (i.e. got an exception before
     *  the alloc event), then none of the after* dependencies can be
     *  satisfied. Immediately raise exception(s).
     */
    if (!flux_jobtap_job_event_posted (p, FLUX_JOBTAP_CURRENT_JOB, "alloc")) {
        raise_exceptions (p, l, "job never started");
        return 0;
    }

    /*  O/w, release dependent jobs based on requisite job result.
     *  Entries will be removed from the list as they are processed.
     */
    if (result != FLUX_JOB_RESULT_COMPLETED)
        release_all (p, l, AFTER_FINISH | AFTER_FAILURE);
    else
        release_all (p, l, AFTER_FINISH | AFTER_SUCCESS);

    /*  Any remaining dependencies can't now be satisfied.
     *   Raise exceptions on any remaining members of list `l`
     */
    raise_exceptions (p, l, "can never be satisfied");

    return 0;
}

/*  In job.state.priority, delete any dependency references in a job
 *   that was dependent on other jobs. This prevents the references
 *   from being released at job completion. See release_references()
 *   call in inactive_cb() above.
 */
static int priority_cb (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    zlistx_t *l = after_refs_check (p);
    if (l) {
        /*  Job has proceeded out of DEPEND state.
         *  Delete any dependency reference list since it will no longer
         *  be used, and we don't need to attempt deref of dependencies
         *  (see bottom of inactive_cb()).
         */
        if (flux_jobtap_job_aux_delete (p, FLUX_JOBTAP_CURRENT_JOB, l) < 0) {
            flux_log_error (flux_jobtap_get_flux (p),
                            "dependency-after: flux_jobtap_job_aux_delete");
        }
    }
    return 0;
}

/*  On start event, release all AFTER_START dependencies
 */
static int start_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    release_all (p, after_list_check (p, 0), AFTER_START);

    /*  This is the only job event we care about, unsubscribe from
     *   future job events
     */
    flux_jobtap_job_unsubscribe (p, FLUX_JOBTAP_CURRENT_JOB);

    return 0;
}

/*  In INACTIVE state, release remaining dependent jobs.
 */
static int inactive_cb (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    /*  Only need to check for dependent jobs if this job has an
     *   embedded dependency list
     */
    release_dependent_jobs (p, after_list_check (p, 0));

    /*  "Release" any references this job had to any dependencies
     *  (references should still exist only if job skipped PRIORITY state.)
     */
    release_dependency_references (p);

    return 0;
}

static json_t *deps_to_json (flux_plugin_t *p)
{
    json_t *o = NULL;
    zlistx_t *l;

    if (!(o = json_array ()))
        return NULL;

    if ((l = global_reflist)) {
        struct after_ref *ref = zlistx_first (l);
        while (ref) {
            struct after_info *info = ref->info;
            json_t *entry = NULL;

            if (!(entry = json_pack ("{s:I s:I s:s s:s}",
                                     "id", ref->id,
                                     "depid", info->depid,
                                     "type", after_typestr (info->type),
                                     "description", info->description))
                || json_array_append_new (o, entry) < 0) {
                json_decref (entry);
                goto error;
            }
            ref = zlistx_next (l);
        }
    }
    return o;
error:
    json_decref (o);
    return NULL;
}

static int query_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    json_t *o = deps_to_json (p);

    if (!o) {
        flux_log (flux_jobtap_get_flux (p),
                  LOG_ERR,
                  "dependency-after: deps_to_json failed");
        return -1;
    }

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:O}",
                              "dependencies", o) < 0)
        flux_log_error (flux_jobtap_get_flux (p),
                        "dependency-after: query_cb: flux_plugin_arg_pack: %s",
                        flux_plugin_arg_strerror (args));
    json_decref (o);
    return 0;
}

static const struct flux_plugin_handler tab[] = {
    { "job.dependency.after",      dependency_after_cb, NULL },
    { "job.dependency.afterok",    dependency_after_cb, NULL },
    { "job.dependency.afterany",   dependency_after_cb, NULL },
    { "job.dependency.afternotok", dependency_after_cb, NULL },
    { "job.state.priority",        priority_cb,         NULL },
    { "job.state.inactive",        inactive_cb,         NULL },
    { "job.event.start",           start_cb,            NULL },
    { "plugin.query",              query_cb,            NULL },
    { 0 }
};

static void reflist_destroy (zlistx_t *l)
{
    zlistx_destroy (&l);
    global_reflist = NULL;
}

int after_plugin_init (flux_plugin_t *p)
{
    if (!(global_reflist = zlistx_new ())
        || flux_plugin_aux_set (p,
                                NULL,
                                global_reflist,
                                (flux_free_f)reflist_destroy) < 0) {
        reflist_destroy (global_reflist);
        return -1;
    }
    return flux_plugin_register (p, ".dependency-after", tab);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

