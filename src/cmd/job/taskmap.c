/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-job taskmap */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>

#include <flux/core.h>
#include <flux/optparse.h>
#include <flux/taskmap.h>

#include "src/common/libutil/log.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libtaskmap/taskmap_private.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"
#include "common.h"

struct optparse_option taskmap_opts[] = {
    { .name = "taskids", .has_arg = 1, .arginfo = "NODEID",
      .usage = "Print idset of tasks on node NODEID",
    },
    { .name = "ntasks", .has_arg = 1, .arginfo = "NODEID",
      .usage = "Print number of tasks on node NODEID",
    },
    { .name = "nodeid", .has_arg = 1, .arginfo = "TASKID",
      .usage = "Print the shell rank/nodeid on which a taskid executed",
    },
    { .name = "hostname", .has_arg = 1, .arginfo = "TASKID",
      .usage = "Print the hostname on which a taskid executed",
    },
    { .name = "to", .has_arg = 1, .arginfo="FORMAT",
      .usage = "Convert an RFC 34 taskmap to another format "
               "(FORMAT can be raw, pmi, or multiline)",
    },
    OPTPARSE_TABLE_END
};

static flux_t *handle = NULL;

static void global_flux_close (void)
{
    flux_close (handle);
}

static flux_t *global_flux_open (void)
{
    if (!handle) {
        if (!(handle = flux_open (NULL, 0)))
            log_err_exit ("flux_open");
        atexit (global_flux_close);
    }
    return handle;
}

static struct taskmap *get_job_taskmap (flux_jobid_t id)
{
    struct taskmap *map;
    flux_t *h;
    flux_future_t *f;

    h = global_flux_open ();

    if (!(f = flux_job_event_watch (h, id, "guest.exec.eventlog", 0)))
        log_err_exit ("flux_job_event_watch");
    while (true) {
        json_t *o;
        json_t *context;
        const char *event;
        const char *name;
        if (flux_job_event_watch_get (f, &event) < 0) {
            if (errno == ENODATA)
                log_msg_exit ("No taskmap found for job");
            if (errno == ENOENT)
                log_msg_exit ("Unable to get job taskmap: no such job");
            log_msg_exit ("waiting for shell.start event: %s",
                          future_strerror (f, errno));
        }
        if (!(o = eventlog_entry_decode (event)))
            log_err_exit ("eventlog_entry_decode");
        if (eventlog_entry_parse (o, NULL, &name, &context) < 0)
            log_err_exit ("eventlog_entry_parse");
        if (streq (name, "shell.start")) {
            flux_error_t error;
            json_t *omap;
            if (!(omap = json_object_get (context, "taskmap"))
                || !(map = taskmap_decode_json (omap, &error)))
                log_msg_exit ("failed to get taskmap from shell.start event");
            json_decref (o);
            flux_job_event_watch_cancel (f);
            break;
        }
        json_decref (o);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    return map;
}

static struct hostlist *job_hostlist (flux_jobid_t id)
{
    flux_t *h;
    flux_future_t *f;
    const char *R;
    struct rlist *rl;
    struct hostlist *hl;

    h = global_flux_open ();

    if (!(f = flux_rpc_pack (h,
                             "job-info.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:[s] s:i}",
                             "id", id,
                             "keys", "R",
                             "flags", 0)))
        log_err_exit ("flux_rpc_pack");
    if (flux_rpc_get_unpack (f, "{s:s}", "R", &R) < 0
        || !(rl = rlist_from_R (R))
        || !(hl = rlist_nodelist (rl)))
        log_err_exit ("failed to get hostlist for job");
    rlist_destroy (rl);
    flux_future_destroy (f);
    return hl;
}

static char *job_nodeid_to_hostname (flux_jobid_t id, int nodeid)
{
    char *result;
    const char *host;
    struct hostlist *hl = job_hostlist (id);
    if (!(host = hostlist_nth (hl, nodeid)))
        log_err_exit ("failed to get hostname for node %d", nodeid);
    result = strdup (host);
    hostlist_destroy (hl);
    return result;
}

int cmd_taskmap (optparse_t *p, int argc, char **argv)
{
    int optindex = optparse_option_index (p);
    struct taskmap *map = NULL;
    int val;
    const char *to;
    char *s;
    flux_jobid_t id;

    if (optindex == argc) {
        optparse_print_usage (p);
        exit (1);
    }
    if (flux_job_id_parse (argv[optindex], &id) < 0) {
        flux_error_t error;
        if (!(map = taskmap_decode (argv[optindex], &error)))
            log_msg_exit ("error decoding taskmap: %s", error.text);
    }
    else
        map = get_job_taskmap (id);

    if ((val = optparse_get_int (p, "taskids", -1)) != -1) {
        const struct idset *ids = taskmap_taskids (map, val);
        if (!ids || !(s = idset_encode (ids, IDSET_FLAG_RANGE)))
            log_err_exit ("No taskids for node %d", val);
        printf ("%s\n", s);
        free (s);
        taskmap_destroy (map);
        return 0;
    }
    if ((val = optparse_get_int (p, "ntasks", -1)) != -1) {
        int result = taskmap_ntasks (map, val);
        if (result < 0)
            log_err_exit ("failed to get task count for node %d", val);
        printf ("%d\n", result);
        taskmap_destroy (map);
        return 0;
    }
    if ((val = optparse_get_int (p, "nodeid", -1)) != -1
        || (val = optparse_get_int (p, "hostname", -1)) != -1) {
        int result = taskmap_nodeid (map, val);
        if (result < 0)
            log_err_exit ("failed to get nodeid for task %d", val);
        if (optparse_hasopt (p, "hostname")) {
            char *host = job_nodeid_to_hostname (id, result);
            printf ("%s\n", host);
            free (host);
        }
        else
            printf ("%d\n", result);
        taskmap_destroy (map);
        return 0;
    }
    if ((to = optparse_get_str (p, "to", NULL))) {
        if (streq (to, "raw"))
            s = taskmap_encode (map, TASKMAP_ENCODE_RAW);
        else if (streq (to, "pmi"))
            s = taskmap_encode (map, TASKMAP_ENCODE_PMI);
        else if (streq (to, "multiline")) {
            for (int i = 0; i < taskmap_total_ntasks (map); i++) {
                printf ("%d: %d\n", i, taskmap_nodeid (map, i));
            }
            taskmap_destroy (map);
            return 0;
        }
        else
            log_msg_exit ("invalid value --to=%s", to);
        if (s == NULL)
            log_err_exit ("failed to convert taskmap to %s", to);
        printf ("%s\n", s);
        free (s);
        taskmap_destroy (map);
        return 0;
    }
    if (!(s = taskmap_encode (map, 0)))
        log_err_exit ("taskmap_encode");
    printf ("%s\n", s);
    free (s);
    taskmap_destroy (map);
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */
