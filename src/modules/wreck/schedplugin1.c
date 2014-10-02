/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

/* schedplugin1.c - backfill sheduling services
 *
 * Update Log:
 *       Aug 7 2014 DAL: File created.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <czmq.h>

#include "util.h"
#include "log.h"
#include "shortjson.h"
#include "plugin.h"
#include "rdl.h"
#include "scheduler.h"

static const char* CORETYPE = "core";

/*
 * find_resources() identifies the all of the resource candidates for the
 * job.  The set of resources returned could be more than the job
 * requires.  A later call to select_resources() will cull this list
 * down to the most appropriate set for the job.  If less resources
 * are found than the job requires, and if the job asks to reserve
 * resources, then *preserve will be set to true.
 *
 * Inputs:  the complete rdl and the job
 * Returns: pointer to the found resources or NULL if no (or not enough)
 *          resources were found
 * Other Outputs: whether the returned resources are to be reserved.
 */
struct rdl *find_resources (flux_t h, struct rdl *rdl, const char *uri,
                         flux_lwj_t *job, bool *preserve)
{
    int32_t cores;
    json_object *args = util_json_object_new_object ();
    json_object *o;
    struct resource *r = NULL;
    struct rdl *frdl = NULL;            /* found rdl */
    bool found = false;

    if (!job || !rdl || !uri) {
        flux_log (h, LOG_ERR, "find_resources invalid arguments");
        goto ret;
    }

    util_json_object_add_string (args, "type", "core");
    util_json_object_add_boolean (args, "available", true);
    frdl = rdl_find (rdl, args);

    if (frdl) {
        if ((r = rdl_resource_get (frdl, uri)) == NULL) {
            flux_log (h, LOG_INFO, "no resources available for job %ld",
                      job->lwj_id);
            goto ret;
        }

        o = rdl_resource_aggregate_json (r);
        if (o) {
            if (util_json_object_get_int (o, "core", &cores)) {
                flux_log (h, LOG_ERR, "find_resources failed to get cores: %s",
                          strerror (errno));
                goto ret;
            } else if (cores >= job->req->ncores) {
                *preserve = false;
                flux_log (h, LOG_DEBUG, "%d cores found for lwj.%ld req: %d",
                          cores, job->lwj_id, job->req->ncores);
                found = true;
            } else if (cores && job->reserve) {
                *preserve = true;
                flux_log (h, LOG_DEBUG, "%d cores reserved for lwj.%ld's req %d",
                          cores, job->lwj_id, job->req->ncores);
                found = true;
            }
            json_object_put (o);
        }
    }
ret:
    if (!found) {
        rdl_destroy (frdl);
        frdl = NULL;
    }

    return frdl;
}

/*
 * select_resource implements the resource selection policy of this
 * plugin.  It establishes the criteria for what constitutes the best
 * resource for the job.  select_resource is recursive, visiting each
 * member of the hierarchy in child-first order.
 *
 * Each resource selected for the job is tagged with the job's ID.  If
 * reserve is reqested, a "reserve" tag is also assigned.
 */
static bool select_resource (flux_t h, struct rdl *rdl, const char *resrc,
                             struct resource *fr, struct rdl_accumulator *a,
                             flux_lwj_t *job, flux_res_t *preq,
                             flux_res_t *palloc, bool reserve)
{
    char *lwjtag = NULL;
    char *uri = NULL;
    const char *type = NULL;
    json_object *o = NULL;
    struct resource *c;
    struct resource *r;
    bool found = false;

    asprintf (&uri, "%s:%s", resrc, rdl_resource_path (fr));
    r = rdl_resource_get (rdl, uri);
    free (uri);

    if (rdl_resource_available (r)) {
        o = rdl_resource_json (r);
        Jget_str (o, "type", &type);
        if (reserve)
            asprintf (&lwjtag, "reserve.lwj.%ld", job->lwj_id);
        else
            asprintf (&lwjtag, "lwj.%ld", job->lwj_id);
        if (preq->nnodes && (strcmp (type, "node") == 0)) {
            preq->nnodes--;
            palloc->nnodes++;
        } else if (preq->ncores && (strcmp (type, CORETYPE) == 0) &&
                   (preq->ncores > preq->nnodes)) {
            /* The (preq->ncores > preq->nnodes) requirement
             * guarantees at least one core per node. */
            rdl_resource_tag (r, lwjtag);
            rdl_accumulator_add (a, r);
            if (!rdl_resource_alloc (r, 1)) {
                preq->ncores--;
                palloc->ncores++;
                flux_log (h, LOG_DEBUG, "selected core: %s",
                          json_object_to_json_string (o));
            } else {
                flux_log (h, LOG_ERR, "failed to select %s",
                          json_object_to_json_string (o));
            }
        }
        free (lwjtag);
        json_object_put (o);

        found = !(preq->nnodes || preq->ncores);

        while (!found && (c = rdl_resource_next_child (fr))) {
            found = select_resource (h, rdl, resrc, c, a, job, preq, palloc,
                                     reserve);
            rdl_resource_destroy (c);
        }
    }

    return found;
}

/*
 * select_resources() selects from the set of resource candidates the
 * best resources for the job.  If reserve is set, whatever resources
 * are selected with be reserved for the job and removed from
 * consideration as candidates for other jobs.
 *
 * Inputs:  the complete rdl, the root of the found resources, and the job
 * Returns: 0 on success
 */
int select_resources (flux_t h, struct rdl *rdl, const char *uri,
                      struct resource *fr, flux_lwj_t *job, bool reserve)
{
    int rc = -1;
    struct rdl_accumulator *a = NULL;
    flux_res_t alloc;
    flux_res_t req;

    req.nnodes = job->req->nnodes;
    req.ncores = job->req->ncores;
    alloc.nnodes = 0;
    alloc.ncores = 0;

    a = rdl_accumulator_create (rdl);
    if (select_resource (h, rdl, uri, fr, a, job, &req, &alloc, reserve)) {
        job->rdl = rdl_accumulator_copy (a);
        rc = 0;
    }

    return rc;
}

/*
 * Recursively search the resource r and update this job's lwj key
 * with the core count per rank (i.e., node for the time being)
 */
static int update_job_cores (flux_t h, struct resource *jr, flux_lwj_t *job,
                             uint64_t *pnode, uint32_t *pcores)
{
    bool imanode = false;
    char *key = NULL;
    char *lwjtag = NULL;
    const char *type = NULL;
    json_object *o = NULL;
    json_object *o2 = NULL;
    json_object *o3 = NULL;
    struct resource *c;
    int rc = 0;

    if (jr) {
        o = rdl_resource_json (jr);
        if (o) {
            flux_log (h, LOG_DEBUG, "considering: %s",
                      json_object_to_json_string (o));
        } else {
            flux_log (h, LOG_ERR, "update_job_cores invalid resource");
            rc = -1;
            goto ret;
        }
    } else {
        flux_log (h, LOG_ERR, "update_job_cores passed a null resource");
        rc = -1;
        goto ret;
    }

    Jget_str (o, "type", &type);
    if (strcmp (type, "node") == 0) {
        *pcores = 0;
        imanode = true;
    } else if (strcmp (type, CORETYPE) == 0) {
        /* we need to limit our allocation to just the tagged cores */
        asprintf (&lwjtag, "lwj.%ld", job->lwj_id);
        Jget_obj (o, "tags", &o2);
        Jget_obj (o2, lwjtag, &o3);
        if (o3) {
            (*pcores)++;
        }
        free (lwjtag);
    }
    json_object_put (o);

    while ((rc == 0) && (c = rdl_resource_next_child (jr))) {
        rc = update_job_cores (h, c, job, pnode, pcores);
        rdl_resource_destroy (c);
    }

    if (imanode) {
        if (asprintf (&key, "lwj.%ld.rank.%ld.cores", job->lwj_id,
                      *pnode) < 0) {
            flux_log (h, LOG_ERR, "update_job_cores key create failed");
            rc = -1;
            goto ret;
        } else if (kvs_put_int64 (h, key, *pcores) < 0) {
            flux_log (h, LOG_ERR, "update_job_cores %ld node failed: %s",
                      job->lwj_id, strerror (errno));
            rc = -1;
            goto ret;
        }
        free (key);
        (*pnode)++;
    }

ret:
    return rc;
}

/*
 * allocate_resources() updates job and resource records in the
 * kvs to reflect the resources' allocation to the job.
 *
 * This plugin creates lwj entries that tell wrexecd how many tasks to
 * launch per node.
 *
 * The key has the form:  lwj.<jobID>.rank.<nodeID>.cores
 * The value will be the number of tasks to launch on that node.
 *
 * Inputs:  uri of the resource and job
 * Returns: 0 on success
 */
int allocate_resources (flux_t h, const char *uri, flux_lwj_t *job)
{
    char *key = NULL;
    char *rdlstr = NULL;
    uint64_t node = 0;
    uint32_t cores = 0;
    struct resource *jr = rdl_resource_get (job->rdl, uri);
    int rc = -1;

    if (jr)
        rc = update_job_cores (h, jr, job, &node, &cores);
    else
        flux_log (h, LOG_ERR, "allocate_resources passed a null resource");

    if (rc == 0) {
        rc = -1;
        rdlstr = rdl_serialize (job->rdl);
        if (!rdlstr) {
            flux_log (h, LOG_ERR, "%ld rdl_serialize failed: %s",
                      job->lwj_id, strerror (errno));
        } else if (asprintf (&key, "lwj.%ld.rdl", job->lwj_id) < 0) {
            flux_log (h, LOG_ERR, "allocate_resources key create failed");
        } else if (kvs_put_string (h, key, rdlstr) < 0) {
            flux_log (h, LOG_ERR, "allocate_resources %ld rdl write failed: %s",
                      job->lwj_id, strerror (errno));
        } else {
            rc = 0;
        }
    }
    free (key);
    free (rdlstr);

    return rc;
}

/*
 * Recursively search the job's resource r and:
 * - remove its lwj tag and
 * - free the resource of this job
 *
 * Returns: 0 on success
 */
static int release_lwj_resource (flux_t h, struct rdl *rdl, const char *resrc,
                                 struct resource *jr, int64_t lwj_id)
{
    char *lwjtag = NULL;
    char *uri = NULL;
    const char *type = NULL;
    int rc = 0;
    json_object *o = NULL;
    struct resource *c;
    struct resource *r;

    asprintf (&uri, "%s:%s", resrc, rdl_resource_path (jr));
    r = rdl_resource_get (rdl, uri);

    if (r) {
        o = rdl_resource_json (r);
        Jget_str (o, "type", &type);
        if (strcmp (type, CORETYPE) == 0) {
            asprintf (&lwjtag, "lwj.%ld", lwj_id);
            rdl_resource_delete_tag (r, lwjtag);
            rdl_resource_free (r, 1);
            flux_log (h, LOG_DEBUG, "%s released: %ld now available",
                      rdl_resource_path (r), rdl_resource_available (r));
            free (lwjtag);
        }
        json_object_put (o);

        while (!rc && (c = rdl_resource_next_child (jr))) {
            rc = release_lwj_resource (h, rdl, resrc, c, lwj_id);
            rdl_resource_destroy (c);
        }
    } else {
        flux_log (h, LOG_ERR, "release_lwj_resource failed to get %s", uri);
        rc = -1;
    }
    free (uri);

    return rc;
}

/*
 * release_resources() visits all the resources allocated to a job,
 * and releases the job's claim on them.  This is mostly a bookkeeping
 * exercise whereby the lwj tag is removed and the associated
 * "allocated" values are decremented accordingly.
 */
int release_resources (flux_t h, struct rdl *rdl, const char *uri,
                       flux_lwj_t *job)
{
    int rc = -1;
    struct resource *jr = rdl_resource_get (job->rdl, uri);

    if (jr) {
        rc = release_lwj_resource (h, rdl, uri, jr, job->lwj_id);
    } else {
        flux_log (h, LOG_ERR, "release_resources failed to get resources: %s",
                  strerror (errno));
    }

    return rc;
}



/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
