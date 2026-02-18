/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dependency-singleton.c
 *
 * Support singleton dependency scheme which places a dependency
 * on a job which is only released when there are no other active
 * jobs of the same userid and job name which are not also already
 * held with a singleton dependency.
 *
 * Notes:
 * - counts of active jobs with the same userid/job-name are maintained
 *   in a global hash
 * - jobs without an explicit name are ignored
 * - jobs submitted with dependency=singleton are placed on a list, also
 *   hashed by userid/job-name
 * - when the active job count for a userid/job-name is decremented and
 *   equals the count of singletons for that same pair, a singleton job
 *   is released and removed from the list
 * - it is an error to submit a singleton job without an explicit job name
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/jobtap.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/hola.h"
#include "ccan/str/str.h"
#include "ccan/ptrint/ptrint.h"

struct singleton_ctx {
    zhashx_t *counts; // uid:job-name => ACTIVE job counts
                      // (with or without singleton dep)
    struct hola *singletons; // uid:job-name => list of struct singleton
                             // (jobs with singleton dep)
};

struct singleton {
    flux_jobid_t id;
};

static struct singleton_ctx *global_ctx = NULL;

static void singleton_ctx_destroy (struct singleton_ctx *sctx)
{
    if (sctx) {
        int saved_errno = errno;
        zhashx_destroy (&sctx->counts);
        hola_destroy (sctx->singletons);
        free (sctx);
        errno = saved_errno;
    }
}

static struct singleton_ctx *singleton_ctx_create (void)
{
    struct singleton_ctx *sctx;
    int flags = HOLA_AUTOCREATE | HOLA_AUTODESTROY;

    if (!(sctx = malloc (sizeof (*sctx)))
        || !(sctx->counts = zhashx_new ())
        || !(sctx->singletons = hola_create (flags)))
        goto error;
    return sctx;
error:
    singleton_ctx_destroy (sctx);
    return NULL;
}

static int singleton_key_create (char *key,
                                 size_t len,
                                 uint32_t userid,
                                 const char *name)
{
    if (snprintf (key, len, "%u:%s", userid, name) >= len)
        return -1;
    return 0;
}

static int singleton_list_push (flux_plugin_t *p,
                                struct singleton_ctx *sctx,
                                const char *key,
                                flux_jobid_t id)
{
    struct singleton *s;

    if (!(s = calloc (1, sizeof (*s)))
        || flux_jobtap_job_aux_set (p, id, NULL, s, (flux_free_f) free) < 0) {
        free (s);
        return -1;
    }
    s->id = id;
    if (!(hola_list_add_end (sctx->singletons, key, s)))
        return -1;

    return 0;
}

static struct singleton *singleton_list_pop (struct singleton_ctx *sctx,
                                             const char *key)
{
    struct singleton *s = hola_list_first (sctx->singletons, key);
    if (s) {
        hola_list_delete (sctx->singletons,
                          key,
                          hola_list_cursor (sctx->singletons, key));
    }
    return s;
}

static void singleton_list_remove_id (struct singleton_ctx *sctx,
                                      const char *key,
                                      flux_jobid_t id)
{
    struct singleton *s = hola_list_first (sctx->singletons, key);
    while (s) {
        if (s->id == id) {
            hola_list_delete (sctx->singletons,
                              key,
                              hola_list_cursor (sctx->singletons, key));
            return;
        }
        s = hola_list_next (sctx->singletons, key);
    }
}

static size_t singleton_list_count (struct singleton_ctx *sctx,
                                    const char *key)
{
    return hola_list_size (sctx->singletons, key);
}

/* Return the current count of ACTIVE jobs with same uid:job-name
 */
static int get_current_active_count (struct singleton_ctx *sctx,
                                     const char *key)
{
    return ptr2int (zhashx_lookup (sctx->counts, key));
}

/* Set the current count of ACTIVE jobs for key (uid:job-name)
 */
static void set_current_active_count (struct singleton_ctx *sctx,
                                      const char *key,
                                      int count)
{
    if (count > 0)
        zhashx_update (sctx->counts, key, int2ptr (count));
    else
        zhashx_delete (sctx->counts, key);
}

/* Update singleton userid:name hash counts with value (1 or -1)
 */
static int singleton_count_update (struct singleton_ctx *sctx,
                                   flux_plugin_t *p,
                                   flux_plugin_arg_t *args,
                                   int value)
{
    flux_jobid_t id;
    struct singleton *s;
    uint32_t userid;
    const char *name = NULL;
    char key [1024];
    int count;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i s:{s:{s:{s?{s?s}}}}}",
                                "id", &id,
                                "userid", &userid,
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "job",
                                    "name", &name) < 0)
        return -1;

    /* Only track jobs with an explicit name */
    if (name == NULL)
        return 0;

    if (singleton_key_create (key, sizeof (key), userid, name) < 0)
        return -1;

    /* If a singleton job goes inactive, remove it from the singleton list
     * to avoid a use-after-free when the job is later destroyed and the
     * aux destructor frees the struct singleton.
     */
    if (value < 0)
        singleton_list_remove_id (sctx, key, id);

    count = get_current_active_count (sctx, key) + value;
    if (count == singleton_list_count (sctx, key)
        && (s = singleton_list_pop (sctx, key))) {
        /*  All active jobs of this uid/name pair are waiting on a singleton
         *  dependency. Pop the next job on the list and release it:
         */
        if (flux_jobtap_dependency_remove (p, s->id, "singleton") < 0) {
            flux_jobtap_raise_exception (p,
                                         s->id,
                                         "dependency",
                                         0,
                                         "failed to remove singleton dependency");
        }
    }
    set_current_active_count (sctx, key, count);
    return 0;
}

static int new_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *data)
{
    return singleton_count_update (global_ctx, p, args, 1);
}

static int inactive_cb (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    return singleton_count_update (global_ctx, p, args, -1);
}


static int dependency_singleton_cb (flux_plugin_t *p,
                                    const char *topic,
                                    flux_plugin_arg_t *args,
                                    void *data)
{
    struct singleton_ctx *sctx = global_ctx;
    flux_jobid_t id;
    uint32_t userid;
    const char *name = NULL;
    char key [1024];
    int count;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:I s:i s:{s:{s:{s?{s?s}}}}}",
                                "id", &id,
                                "userid", &userid,
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "job",
                                    "name", &name) < 0)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "failed to unpack plugin args: %s",
                                       flux_plugin_arg_strerror (args));
    if (name == NULL)
        return flux_jobtap_reject_job (p,
                                       args,
                                       "singleton jobs require a job name");

    if (singleton_key_create (key, sizeof (key), userid, name) < 0)
        return flux_jobtap_reject_job (p, args, "error creating singleton key");

    /* Get current count of matching jobs in PRIORITY|SCHED|RUN|CLEANUP state.
     * If there are no other matching jobs then release this one immediately.
     *
     * Note: The current job is not included in the `counts` hash yet since
     * `job.dependency.*` callbacks run before `job.new`, which is only called
     * on valid jobs.
     */
    if ((count = get_current_active_count (sctx, key)) == 0)
        return 0;

    if (flux_jobtap_dependency_add (p, id, "singleton") < 0)
        return flux_jobtap_reject_job (p, args, "failed to add dependency");

    return singleton_list_push (p, sctx, key, id);
}

static json_t *singleton_list_to_json (const char *key)
{
    json_t *o;
    zlistx_t *l;
    struct singleton *s;

    if (!(o = json_array ()))
        return NULL;

    if (!(l = hola_hash_lookup (global_ctx->singletons, key)))
        return o;

    s = zlistx_first (l);
    while (s) {
        json_t *id = json_integer (s->id);
        if (!id || json_array_append_new (o, id)) {
            json_decref (id);
            goto error;
        }
        s = zlistx_next (l);
    }
    return o;
error:
    json_decref (o);
    return NULL;
}

static json_t *singletons_to_json (void)
{
    json_t *o = NULL;
    zlistx_t *keys = NULL;
    const char *key;

    if (!(keys = zhashx_keys (global_ctx->counts))
        || !(o = json_object ()))
        goto error;

    key = zlistx_first (keys);
    while (key) {
        json_t *singletons;
        json_t *entry = NULL;
        int count = get_current_active_count (global_ctx, key);

        if (!(singletons = singleton_list_to_json (key))
            || !(entry = json_pack ("{s:i s:O}",
                                    "count", count,
                                    "singletons", singletons))
            || json_object_set_new (o, key, entry) < 0) {
            json_decref (entry);
            json_decref (singletons);
            goto error;
        }
        json_decref (singletons);
        key = zlistx_next (keys);
    }
    zlistx_destroy (&keys);
    return o;
error:
    json_decref (o);
    zlistx_destroy (&keys);
    return NULL;
}

static int query_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    int rc;
    json_t *o;
    if (!(o = singletons_to_json ()))
        return -1;
    rc = flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "O", o);
    json_decref (o);
    return rc;
}

static const struct flux_plugin_handler tab[] = {
    { "job.dependency.singleton",  dependency_singleton_cb, NULL },
    { "job.new",                   new_cb,                  NULL },
    { "job.state.inactive",        inactive_cb,             NULL },
    { "plugin.query",              query_cb,                NULL },
    { 0 },
};

int singleton_plugin_init (flux_plugin_t *p)
{
    if (!(global_ctx = singleton_ctx_create ())
        || flux_plugin_aux_set (p,
                                NULL,
                                global_ctx,
                                (flux_free_f) singleton_ctx_destroy) < 0) {
        singleton_ctx_destroy (global_ctx);
        return -1;
    }
    return flux_plugin_register (p, ".dependency-singleton", tab);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

