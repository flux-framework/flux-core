/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/read_all.h"

#include "internal.h"
#include "info.h"
#include "jobspec.h"

/* Append string 's' to JSON array 'array'.
 * Return 0 on success, -1 on failure.
 */
static int array_append_string (json_t *array, const char *s)
{
    json_t *o;

    if (!(o = json_string (s)) || json_array_append_new (array, o) < 0) {
        json_decref (o);
        return -1;
    }
    return 0;
}

/* If either *jobspec or *R is NULL, fetch it from future and assign.
 * Return 0 on success, -1 on failure (and log error).
 * N.B. assigned values remain valid until future is destroyed.
 */
static int lookup_job_info_get (flux_future_t *f,
                                const char **jobspec,
                                const char **R)
{
    if (!*jobspec && flux_rpc_get_unpack (f, "{s:s}", "jobspec", jobspec) < 0)
        goto error;
    if (!*R && flux_rpc_get_unpack (f, "{s:s}", "R", R) < 0)
        goto error;
    return 0;
error:
    shell_log_error ("job-info: %s", future_strerror (f, errno));
    return -1;
}

/* If either jobspec or R is NULL, fetch it from the job-info service.
 * Return future on success or NULL on failure (and log error).
 */
static flux_future_t *lookup_job_info (flux_t *h,
                                       flux_jobid_t jobid,
                                       const char *jobspec,
                                       const char *R)
{
    json_t *keys;
    flux_future_t *f;

    if (!(keys = json_array ())
            || (!R && array_append_string (keys, "R") < 0)
            || (!jobspec && array_append_string (keys, "jobspec") < 0)) {
        shell_log_error ("error building json array");
        return NULL;
    }
    f = flux_rpc_pack (h,
                       "job-info.lookup",
                       FLUX_NODEID_ANY,
                       0,
                       "{s:I s:O s:i}",
                       "id", jobid,
                       "keys", keys,
                       "flags", 0);
    if (!f)
        shell_log_error ("error sending job-info request");
    json_decref (keys);
    return f;
}

/* Read content of file 'optarg' and return it or NULL on failure (log error).
 * Caller must free returned result.
 */
static char *parse_arg_file (const char *optarg)
{
    int fd;
    ssize_t size;
    void *buf = NULL;

    if (!strcmp (optarg, "-"))
        fd = STDIN_FILENO;
    else {
        if ((fd = open (optarg, O_RDONLY)) < 0) {
            shell_log_errno ("error opening %s", optarg);
            return NULL;
        }
    }
    if ((size = read_all (fd, &buf)) < 0)
        shell_log_errno ("error reading %s", optarg);
    if (fd != STDIN_FILENO)
        (void)close (fd);
    return buf;
}

/* If option 'name' exists, read it as a file and exit on failure.
 * O/w, return NULL.
 */
static char *optparse_check_and_loadfile (optparse_t *p, const char *name)
{
    char *result = NULL;
    const char *path = optparse_get_str (p, name, NULL);
    if (path) {
        if (!(result = parse_arg_file (path)))
            exit (1);
        return result;
    }
    return NULL;
}

/*  Fetch jobinfo (jobspec, R) from job-info service if not provided on
 *   command line, and parse.
 */
static int shell_init_jobinfo (flux_shell_t *shell,
                               struct shell_info *info,
                               const char *jobspec,
                               const char *R)
{
    int rc = -1;
    flux_future_t *f = NULL;
    json_error_t error;

    if (!R || !jobspec) {
        /* Fetch missing jobinfo from broker job-info service */
        if (shell->standalone) {
            shell_log_error ("Invalid arguments: standalone and R/jobspec are unset");
            return -1;
        }
        if (!(f = lookup_job_info (shell->h, shell->jobid, jobspec, R))
                || lookup_job_info_get (f, &jobspec, &R) < 0)
            goto out;
    }
    if (!(info->jobspec = jobspec_parse (jobspec, &error))) {
        shell_log_error ("error parsing jobspec: %s", error.text);
        goto out;
    }
    if (!(info->rcalc = rcalc_create (R))) {
        shell_log_error ("error decoding R");
        goto out;
    }
    rc = 0;
out:
    flux_future_destroy (f);
    return rc;
}

struct shell_info *shell_info_create (flux_shell_t *shell)
{
    struct shell_info *info;
    char *R = NULL;
    char *jobspec = NULL;
    int target_rank = shell->target_rank;

    if (!(info = calloc (1, sizeof (*info)))) {
        shell_log_errno ("shell_info_create");
        return NULL;
    }
    info->jobid = shell->jobid;

    /*  Check for jobspec and/or R on cmdline:
     */
    jobspec = optparse_check_and_loadfile (shell->p, "jobspec");
    R = optparse_check_and_loadfile (shell->p, "resources");

    if (shell_init_jobinfo (shell, info, jobspec, R) < 0)
        goto error;

    /* Done with potentially allocated jobspec, R strings */
    free (jobspec);
    free (R);

   if (rcalc_distribute (info->rcalc, info->jobspec->task_count) < 0) {
        shell_log_error ("error distributing %d tasks over R",
                         info->jobspec->task_count);
        goto error;
    }
    if (rcalc_get_rankinfo (info->rcalc, target_rank, &info->rankinfo) < 0) {
        shell_log_error ("error fetching rankinfo for rank %d", target_rank);
        goto error;
    }
    info->shell_size = rcalc_total_nodes (info->rcalc);
    info->shell_rank = info->rankinfo.nodeid;
    return info;
error:
    shell_info_destroy (info);
    return NULL;
}

void shell_info_destroy (struct shell_info *info)
{
    if (info) {
        int saved_errno = errno;
        jobspec_destroy (info->jobspec);
        rcalc_destroy (info->rcalc);
        free (info);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
