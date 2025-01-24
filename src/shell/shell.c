/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell mainline */
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <locale.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/liboptparse/optparse.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/basename.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libtaskmap/taskmap_private.h"
#include "ccan/str/str.h"

#include "internal.h"
#include "builtins.h"
#include "info.h"
#include "svc.h"
#include "task.h"
#include "rc.h"
#include "log.h"
#include "mustache.h"

static char *shell_name = "flux-shell";
static const char *shell_usage = "[OPTIONS] JOBID";

static struct optparse_option shell_opts[] =  {
    { .name = "reconnect", .has_arg = 0,
      .usage = "Attempt to reconnect if broker connection is lost" },
    OPTPARSE_TABLE_END
};

static void shell_events_subscribe (flux_shell_t *shell);

/* Parse optarg as a jobid rank and assign to 'jobid'.
 * Return 0 on success or -1 on failure (log error).
 */
static int parse_jobid (const char *optarg, flux_jobid_t *jobid)
{
    unsigned long long i;
    char *endptr;

    errno = 0;
    i = strtoull (optarg, &endptr, 10);
    if (errno != 0)
        return shell_log_errno ("error parsing jobid");
    if (*endptr != '\0')
        return shell_log_errno ("error parsing jobid: garbage follows number");
    *jobid = i;
    return 0;
}

static void task_completion_cb (struct shell_task *task, void *arg)
{
    struct flux_shell *shell = arg;

    shell_debug ("task %d complete status=%d", task->rank, task->rc);

    shell->current_task = task;
    if (plugstack_call (shell->plugstack, "task.exit", NULL) < 0)
        shell_log_errno ("task.exit plugin(s) failed");
    shell->current_task = NULL;

    if (flux_shell_remove_completion_ref (shell, "task%d", task->rank) < 0)
        shell_log_errno ("failed to remove task%d completion reference",
                         task->rank);
}

int flux_shell_setopt (flux_shell_t *shell,
                       const char *name,
                       const char *json_str)
{
    json_error_t err;
    json_t *o;
    /* If flux_shell_setopt (shell, name, NULL), delete option:
     */
    if (!json_str)
        return json_object_del (shell->info->jobspec->options, name);

    if (!(o = json_loads (json_str, JSON_DECODE_ANY, &err)))
        return -1;
    return json_object_set_new (shell->info->jobspec->options, name, o);
}

int flux_shell_setopt_pack (flux_shell_t *shell,
                            const char *name,
                            const char *fmt, ...)
{
    json_t *o;
    json_error_t err;
    va_list ap;

    va_start (ap, fmt);
    o = json_vpack_ex (&err, 0, fmt, ap);
    va_end (ap);
    if (!o)
        return -1;
    return json_object_set_new (shell->info->jobspec->options, name, o);
}

int flux_shell_getopt (flux_shell_t *shell, const char *name, char **json_str)
{
    json_t *o;

    if (!shell || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!shell->info->jobspec->options
        || !(o = json_object_get (shell->info->jobspec->options, name)))
        return 0;

    /*  NB: Use of JSON_ENCODE_ANY is purposeful here since we are encoding
     *   just a fragment of the shell.options object, and these options
     *   themselves do not have a requirement of being JSON objects.
     */
    if (json_str) {
        char *s = json_dumps (o, JSON_COMPACT|JSON_ENCODE_ANY);
        if (!s) {
            errno = ENOMEM;
            return -1;
        }
        *json_str = s;
    }
    return 1;
}

int flux_shell_getopt_unpack (flux_shell_t *shell,
                              const char *name,
                              const char *fmt, ...)
{
    int rc;
    json_error_t err;
    json_t *o;
    va_list ap;

    if (!shell || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!shell->info->jobspec->options
        || !(o = json_object_get (shell->info->jobspec->options, name)))
        return 0;

    va_start (ap, fmt);
    /*  Add JSON_DECODE_ANY since we're parsing a fragment of
     *   attributes.system.shell.options which may not be an object or array.
     */
    if ((rc = json_vunpack_ex (o, &err, JSON_DECODE_ANY, fmt, ap)) < 0)
        shell_log_error ("shell_getopt: unpack error: %s", err.text);
    else
        rc = 1;
    va_end (ap);
    return rc;
}

static void shell_parse_cmdline (flux_shell_t *shell, int argc, char *argv[])
{
    int optindex;
    optparse_t *p = optparse_create (shell_name);

    if (p == NULL)
        shell_die (1, "optparse_create");
    if (optparse_add_option_table (p, shell_opts) != OPTPARSE_SUCCESS)
        shell_die (1, "optparse_add_option_table failed");
    if (optparse_set (p, OPTPARSE_USAGE, shell_usage) != OPTPARSE_SUCCESS)
        shell_die (1, "optparse_set usage failed");
    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    /* Parse required positional argument.
     */
    if (optindex != argc - 1) {
        optparse_print_usage (p);
        exit (1);
    }
    if (parse_jobid (argv[optindex++], &shell->jobid) < 0)
        exit (1);

    shell->p = p;
}

static int try_reconnect (flux_t *h)
{
    int rc = -1;
    flux_future_t *f;

    if (flux_reconnect (h) < 0) {
        if (errno == ENOSYS)
            shell_die (1, "reconnect not implemented by connector");
        return -1;
    }

    /*  Wait for broker to enter RUN state.
     *
     *  RPC may fail if broker is still shutting down. In that case, return
     *   error so that we reconnect again and retry.
     */
    if (!(f = flux_rpc (h, "state-machine.wait", NULL, FLUX_NODEID_ANY, 0)))
        shell_die (1, "could not send state-machine.wait RPC");
    rc = flux_rpc_get (f, NULL);

    flux_future_destroy (f);
    return rc;
}

static int reconnect (flux_t *h, void *arg)
{
    flux_future_t *f;
    flux_shell_t *shell = arg;

    shell_log_errno ("broker");
    while (try_reconnect (h) < 0)
        sleep (2);

    shell_events_subscribe (shell);

    if (!(f = flux_service_register (h, shell_svc_name (shell->svc))))
        shell_die (1, "could not re-register shell service name");
    if (flux_rpc_get (f, NULL) < 0)
        shell_die (1, "flux_service_register: %s", future_strerror (f, errno));
    flux_future_destroy (f);

    if (plugstack_call (shell->plugstack, "shell.reconnect", NULL) < 0)
        shell_log_errno ("shell.reconnect");

    if (shell_eventlogger_reconnect (shell->ev) < 0)
        shell_log_errno ("shell_eventlogger_reconnect");

    shell_log ("broker: reconnected");
    return 0;
}

static uid_t get_instance_owner (flux_t *h)
{
    const char *s;
    char *endptr;
    int id;

    if (!(s = flux_attr_get (h, "security.owner"))) {
        shell_log_errno ("error fetching security.owner attribute");
        return (uid_t) 0;
    }
    errno = 0;
    id = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0') {
        shell_log_error ("error parsing security.owner=%s", s);
        return (uid_t) 0;
    }
    return (uid_t) id;
}

static void shell_connect_flux (flux_shell_t *shell)
{
    uint32_t rank;
    int flags = optparse_hasopt (shell->p, "reconnect") ? FLUX_O_RPCTRACK : 0;

    if (!(shell->h = flux_open (NULL, flags)))
        shell_die_errno (1, "flux_open");

    if (optparse_hasopt (shell->p, "reconnect"))
        flux_comms_error_set (shell->h, reconnect, shell);

    /*  Set reactor for flux handle to our custom created reactor.
     */
    flux_set_reactor (shell->h, shell->r);

    /*  Fetch local rank
     */
    if (flux_get_rank (shell->h, &rank) < 0)
        shell_log_errno ("error fetching broker rank");
    shell->broker_rank = rank;

    shell->broker_owner = get_instance_owner (shell->h);

    if (plugstack_call (shell->plugstack, "shell.connect", NULL) < 0)
        shell_log_errno ("shell.connect");
}

flux_shell_t *flux_plugin_get_shell (flux_plugin_t *p)
{
    return flux_plugin_aux_get (p, "flux::shell");
}

flux_t *flux_shell_get_flux (flux_shell_t *shell)
{
    return shell->h;
}

int flux_shell_aux_set (flux_shell_t *shell,
                        const char *key,
                        void *val,
                        flux_free_f free_fn)
{
    if (!shell || (!key && !val)) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&shell->aux, key, val, free_fn);
}

void * flux_shell_aux_get (flux_shell_t *shell, const char *key)
{
    if (!shell || !key) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (shell->aux, key);
}

const char * flux_shell_getenv (flux_shell_t *shell, const char *name)
{
    json_t *val;
    if (!shell || !name) {
        errno = EINVAL;
        return NULL;
    }
    val = json_object_get (shell->info->jobspec->environment, name);
    if (val)
        return json_string_value (val);
    return NULL;
}

int flux_shell_get_environ (flux_shell_t *shell, char **json_str)
{
    char *s;
    if (!shell || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(s = json_dumps (shell->info->jobspec->environment, JSON_COMPACT))) {
        errno = ENOMEM;
        return -1;
    }
    *json_str = s;
    return 0;
}

static int object_set_string (json_t *dict, const char *name, const char *val)
{
    json_t *o;

    if (!dict || !name || !val) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = json_string (val)))
        goto nomem;
    if (json_object_set_new (dict, name, o) < 0) {
        json_decref (o);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

int flux_shell_setenvf (flux_shell_t *shell,
                        int overwrite,
                        const char *name,
                        const char *fmt,
                        ...)
{
    json_t *env;
    va_list ap;
    char *val;
    int rc;

    if (!shell || !name || !fmt) {
        errno = EINVAL;
        return -1;
    }

    /* Always update jobspec environment so that this environment update
     * is reflected in a later flux_shell_getenv(3).
     */
    env = shell->info->jobspec->environment;
    if (!overwrite && json_object_get (env, name))
        return 0;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc < 0 || object_set_string (env, name, val) < 0) {
        ERRNO_SAFE_WRAP (free, val);
        return -1;
    }

    if (!shell->tasks)
        goto out;

    /*  If tasks exist, also apply environment update to all tasks
     */
    flux_shell_task_t *task = zlist_first (shell->tasks);
    while (task) {
        flux_cmd_t *cmd = flux_shell_task_cmd (task);
        if (flux_cmd_setenvf (cmd, overwrite, name, "%s", val) < 0) {
            ERRNO_SAFE_WRAP (free, val);
            return -1;
        }
        task = zlist_next (shell->tasks);
    }
out:
    free (val);
    return 0;
}

int flux_shell_unsetenv (flux_shell_t *shell, const char *name)
{
    if (!shell || !name) {
        errno = EINVAL;
        return -1;
    }

    /* Always apply unset to jobspec environment so this unsetenv is
     * reflected in a subsequent flux_shell_getenv(3).
     */
    if (json_object_del (shell->info->jobspec->environment,name) < 0) {
        errno = ENOENT;
        return -1;
    }

    if (!shell->tasks)
        return 0;

    /*  Also, unset variable in all tasks
     */
    flux_shell_task_t *task = zlist_first (shell->tasks);
    while (task) {
        flux_cmd_t *cmd = flux_shell_task_cmd (task);
        (void) flux_cmd_unsetenv (cmd, name);
        task = zlist_next (shell->tasks);
    }
    return 0;
}

int flux_shell_get_hwloc_xml (flux_shell_t *shell, const char **xmlp)
{
    if (!shell || !shell->info || !xmlp) {
        errno = EINVAL;
        return -1;
    }
    *xmlp = shell->info->hwloc_xml;
    return 0;
}

const struct taskmap *flux_shell_get_taskmap (flux_shell_t *shell)
{
    if (!shell || !shell->info) {
        errno = EINVAL;
        return NULL;
    }
    return shell->info->taskmap;
}

static struct hostlist *hostlist_from_R (flux_shell_t *shell)
{
    size_t i;
    json_t *nodelist;
    json_t *val;
    struct hostlist *hl = NULL;

    if (flux_shell_info_unpack (shell,
                                "{s:{s:{s:o}}}",
                                "R",
                                 "execution",
                                  "nodelist", &nodelist) < 0) {
        shell_log_errno ("unable to get job nodelist");
        return NULL;
    }
    if (!(hl = hostlist_create ())) {
        shell_log_errno ("hostlist_create");
        return NULL;
    }
    json_array_foreach (nodelist, i, val) {
        const char *host = json_string_value (val);
        if (!host)
            goto error;
        if (hostlist_append (hl, host) < 0) {
            shell_log_errno ("hostlist_append %s", host);
            goto error;
        }
    }
    return hl;
error:
    hostlist_destroy (hl);
    return NULL;
}

const struct hostlist *flux_shell_get_hostlist (flux_shell_t *shell)
{
    if (!shell || !shell->info) {
        errno = EINVAL;
        return NULL;
    }
    if (!shell->info->hostlist)
        shell->info->hostlist = hostlist_from_R (shell);
    return shell->info->hostlist;
}

static json_t *flux_shell_get_info_object (flux_shell_t *shell)
{
    json_error_t err;
    json_t *o = NULL;

    if (!shell->info)
        return NULL;

    if ((o = flux_shell_aux_get (shell, "shell::info")))
        return o;

    if (!(o = json_pack_ex (&err, 0,
                            "{ s:I s:i s:i s:i s:i s:s s:O s:O s:{ s:i }}",
                            "jobid", shell->info->jobid,
                            "rank",  shell->info->shell_rank,
                            "instance_owner", (int) shell->broker_owner,
                            "size",  shell->info->shell_size,
                            "ntasks", shell->info->total_ntasks,
                            "service", shell_svc_name (shell->svc),
                            "jobspec", shell->info->jobspec->jobspec,
                            "R", shell->info->R,
                            "options",
                               "verbose", shell->verbose)))
        return NULL;
    if (flux_shell_aux_set (shell,
                            "shell::info",
                            o,
                            (flux_free_f) json_decref) < 0) {
        json_decref (o);
        o = NULL;
    }
    return o;
}

int flux_shell_get_info (flux_shell_t *shell, char **json_str)
{
    json_t *o;
    char *s;

    if (!shell || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_info_object (shell)))
        return -1;
    if (!(s = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        return -1;
    }
    *json_str = s;
    return 0;
}

int flux_shell_info_vunpack (flux_shell_t *shell, const char *fmt, va_list ap)
{
    json_t *o;
    json_error_t err;
    if (!shell || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_info_object (shell)))
        return -1;
    return json_vunpack_ex (o, &err, 0, fmt, ap);
}

int flux_shell_info_unpack (flux_shell_t *shell, const char *fmt, ...)
{
    int rc;
    va_list ap;

    va_start (ap, fmt);
    rc = flux_shell_info_vunpack (shell, fmt, ap);
    va_end (ap);
    return rc;
}

static char *get_rank_task_idset (struct taskmap *map, int nodeid)
{
    const struct idset *ids;
    if (!(ids = taskmap_taskids (map, nodeid))) {
        shell_log_errno ("unable to get taskids set for rank %d", nodeid);
        return NULL;
    }
    return idset_encode (ids, IDSET_FLAG_RANGE);
}

static json_t *flux_shell_get_rank_info_object (flux_shell_t *shell, int rank)
{
    json_t *o;
    json_error_t error;
    char key [128];
    char *taskids = NULL;
    struct taskmap *map;
    struct rcalc_rankinfo rankinfo;
    struct hostlist *hl;
    const char *nodename;

    if (!shell->info)
        return NULL;

    if (rank == -1)
        rank = shell->info->shell_rank;

    if (rank < 0 || rank > shell->info->shell_size - 1) {
        errno = EINVAL;
        return NULL;
    }
    if (snprintf (key, sizeof (key), "shell::rinfo%d", rank) >= sizeof (key))
        return NULL;

    if ((o = flux_shell_aux_get (shell, key)))
        return o;

    map = shell->info->taskmap;
    if (!(taskids = get_rank_task_idset (map, rank)))
        return NULL;

    if (rcalc_get_nth (shell->info->rcalc, rank, &rankinfo) < 0)
        return NULL;

    /* Note: Drop const on `struct hostlist *` here. The cursor will be
     * moved, but this should be fine here since the hostlist itself is not
     * changing.
     */
    if (!(hl = (struct hostlist *) flux_shell_get_hostlist (shell))
        || !(nodename = hostlist_nth (hl, rank)))
        return NULL;

    o = json_pack_ex (&error,
                      0,
                      "{s:i s:s s:i s:i s:s s:{s:i s:s s:s?}}",
                      "id", rank,
                      "name", nodename,
                      "broker_rank", rankinfo.rank,
                      "ntasks", taskmap_ntasks (map, rank),
                      "taskids", taskids,
                      "resources",
                       "ncores", rankinfo.ncores,
                       "cores", rankinfo.cores,
                       "gpus",  rankinfo.gpus);
    free (taskids);

    if (o == NULL)
        return NULL;

    if (flux_shell_aux_set (shell, key, o, (flux_free_f) json_decref) < 0) {
        json_decref (o);
        return NULL;
    }

    return o;
}

int flux_shell_get_rank_info (flux_shell_t *shell,
                              int shell_rank,
                              char **json_str)
{
    json_t *o = NULL;
    char *s;

    if (!shell || !json_str || shell_rank < -1) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_rank_info_object (shell, shell_rank)))
        return -1;
    if (!(s = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        return -1;
    }
    *json_str = s;
    return 0;
}

int flux_shell_rank_info_vunpack (flux_shell_t *shell,
                                  int shell_rank,
                                  const char *fmt,
                                  va_list ap)
{
    json_t *o;
    json_error_t err;
    if (!shell || !fmt || shell_rank < -1) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_rank_info_object (shell, shell_rank)))
        return -1;
    return json_vunpack_ex (o, &err, 0, fmt, ap);
}

int flux_shell_rank_info_unpack (flux_shell_t *shell,
                                 int shell_rank,
                                 const char *fmt, ...)
{
    int rc;
    va_list ap;

    va_start (ap, fmt);
    rc = flux_shell_rank_info_vunpack (shell, shell_rank, fmt, ap);
    va_end (ap);
    return rc;
}

static json_t *flux_shell_get_jobspec_info_object (flux_shell_t *shell)
{
    json_t *o = NULL;
    struct jobspec *jobspec;
    if (!(jobspec = shell->info->jobspec))
        return NULL;

    if ((o = flux_shell_aux_get (shell, "shell::jobspec_info")))
        return o;

    /*  Only v1 supported for now:
     */
    if (jobspec->version == 1) {
        o = json_pack ("{s:i s:i s:i s:i s:i s:i}",
                       "version", jobspec->version,
                       "ntasks", jobspec->task_count,
                       "nslots", jobspec->slot_count,
                       "cores_per_slot", jobspec->cores_per_slot,
                       "nnodes", jobspec->node_count,
                       "slots_per_node", jobspec->slots_per_node);
    }
    else
        o = json_pack ("{s:i}", "version", jobspec->version);

    if (o == NULL)
        return NULL;

    if (flux_shell_aux_set (shell,
                            "shell::jobspec_info",
                            o,
                            (flux_free_f) json_decref) < 0) {
        json_decref (o);
        return NULL;
    }
    return o;
}

int flux_shell_get_jobspec_info (flux_shell_t *shell, char **json_str)
{
    json_t *o;
    char *s;

    if (!shell || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_jobspec_info_object (shell)))
        return -1;
    if (!(s = json_dumps (o, JSON_COMPACT))) {
        errno = ENOMEM;
        return -1;
    }
    *json_str = s;
    return 0;
}

int flux_shell_jobspec_info_vunpack (flux_shell_t *shell,
                                     const char *fmt,
                                     va_list ap)
{
    json_t *o;
    json_error_t err;
    if (!shell || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = flux_shell_get_jobspec_info_object (shell)))
        return -1;
    return json_vunpack_ex (o, &err, 0, fmt, ap);
}


int flux_shell_jobspec_info_unpack (flux_shell_t *shell,
                                    const char *fmt, ...)
{
    int rc;
    va_list ap;

    va_start (ap, fmt);
    rc = flux_shell_jobspec_info_vunpack (shell, fmt, ap);
    va_end (ap);
    return rc;
}


int flux_shell_add_event_handler (flux_shell_t *shell,
                                  const char *subtopic,
                                  flux_msg_handler_f cb,
                                  void *arg)
{
    struct flux_match match = FLUX_MATCH_EVENT;
    char *topic;
    flux_msg_handler_t *mh = NULL;

    if (!shell || !shell->h || !subtopic || !cb) {
        errno = EINVAL;
        return -1;
    }
    if (asprintf (&topic,
                  "shell-%ju.%s",
                  (uintmax_t) shell->jobid,
                  subtopic) < 0) {
        shell_log_errno ("add_event: asprintf");
        return -1;
    }
    match.topic_glob = topic;
    if (!(mh = flux_msg_handler_create (shell->h, match, cb, arg))) {
        shell_log_errno ("add_event: flux_msg_handler_create");
        free (topic);
        return -1;
    }
    free (topic);

    /*  Destroy msg handler on flux_close (h) */
    flux_aux_set (shell->h, NULL, mh, (flux_free_f) flux_msg_handler_destroy);
    flux_msg_handler_start (mh);
    return 0;
}

struct service_wrap_arg
{
    flux_shell_t *shell;
    flux_msg_handler_f cb;
    void *arg;
};

static void shell_service_wrap (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct service_wrap_arg *sarg = arg;

    if (shell_svc_allowed (sarg->shell->svc, msg) < 0)
        goto error;
    (*sarg->cb) (h, mh, msg, sarg->arg);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond");
}

static struct service_wrap_arg *
service_wrap_arg_create (flux_shell_t *shell,
                        flux_msg_handler_f cb,
                        void *arg)
{
    struct service_wrap_arg *sarg = calloc (1, sizeof (*sarg));
    if (!sarg)
        return NULL;
    sarg->shell = shell;
    sarg->cb = cb;
    sarg->arg = arg;
    return sarg;
}

int flux_shell_service_register (flux_shell_t *shell,
                                 const char *method,
                                 flux_msg_handler_f cb,
                                 void *arg)
{
    struct service_wrap_arg *sarg = NULL;

    if (!shell || !method || !cb) {
        errno = EINVAL;
        return -1;
    }
    if (!(sarg = service_wrap_arg_create (shell, cb, arg)))
        return -1;

    if (flux_shell_aux_set (shell, NULL, sarg, free) < 0) {
        free (sarg);
        return -1;
    }

    return shell_svc_register (shell->svc,
                               method,
                               shell_service_wrap,
                               sarg);
}

flux_future_t *flux_shell_rpc_pack (flux_shell_t *shell,
                                    const char *method,
                                    int shell_rank,
                                    int flags,
                                    const char *fmt, ...)
{
    flux_future_t *f;
    va_list ap;
    if (!shell || !method || shell_rank < 0 || !fmt) {
        errno = EINVAL;
        return NULL;
    }
    va_start (ap, fmt);
    f = shell_svc_vpack (shell->svc, method, shell_rank, flags, fmt, ap);
    va_end (ap);
    return f;
}


int flux_shell_plugstack_call (flux_shell_t *shell,
                               const char *topic,
                               flux_plugin_arg_t *args)
{
    if (!shell || !topic) {
        errno = EINVAL;
        return -1;
    }
    return plugstack_call (shell->plugstack, topic, args);
}



flux_shell_task_t *flux_shell_current_task (flux_shell_t *shell)
{
    if (!shell) {
        errno = EINVAL;
        return NULL;
    }
    return shell->current_task;
}

flux_shell_task_t *flux_shell_task_first (flux_shell_t *shell)
{
    if (!shell) {
        errno = EINVAL;
        return NULL;
    }
    if (!shell->tasks) {
        errno = EAGAIN;
        return NULL;
    }
    return zlist_first (shell->tasks);
}

flux_shell_task_t *flux_shell_task_next (flux_shell_t *shell)
{
    if (!shell) {
        errno = EINVAL;
        return NULL;
    }
    if (!shell->tasks) {
        errno = EAGAIN;
        return NULL;
    }
    return zlist_next (shell->tasks);
}

static void shell_events_subscribe (flux_shell_t *shell)
{
    if (shell->h) {
        char *topic;
        if (asprintf (&topic, "shell-%ju.", (uintmax_t) shell->jobid) < 0)
            shell_die_errno (1, "shell subscribe: asprintf");
        if (flux_event_subscribe (shell->h, topic) < 0)
            shell_die_errno (1, "shell subscribe: flux_event_subscribe");
        free (topic);
    }
}

static int shell_max_task_exit (flux_shell_t *shell)
{
    /* Process completed tasks:
     * - reduce exit codes to shell 'rc'
     *
     * NB: shell->rc may already be initialized to non-zero if
     * another shell component failed and wanted to ensure that
     * shell exits with error.
     */
    int rc = shell->rc;
    if (shell->tasks) {
        struct shell_task *task = zlist_first (shell->tasks);

        while (task) {
            if (rc < task->rc)
                rc = task->rc;
            task = zlist_next (shell->tasks);
        }
    }
    return rc;
}

static void shell_finalize (flux_shell_t *shell)
{
    struct plugstack *plugstack = shell->plugstack;

    if (shell->tasks) {
        struct shell_task *task;
        while ((task = zlist_pop (shell->tasks)))
            shell_task_destroy (task);
        zlist_destroy (&shell->tasks);
    }
    aux_destroy (&shell->aux);

    /*  Set shell->plugstack to NULL *before* calling plugstack_destroy()
     *   to notify shell components that the plugin stack is no longer
     *   safe to use.
     */
    shell->plugstack = NULL;
    plugstack_destroy (plugstack);

    mustache_renderer_destroy (shell->mr);
    shell_eventlogger_destroy (shell->ev);
    shell_svc_destroy (shell->svc);
    shell_info_destroy (shell->info);

    flux_reactor_destroy (shell->r);
    flux_close (shell->h);

    optparse_destroy (shell->p);

    zhashx_destroy (&shell->completion_refs);
}

static void item_free (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

static const char *shell_conf_get (const char *name)
{
    return flux_conf_builtin_get (name, FLUX_CONF_AUTO);
}

static void get_protocol_fd (int *pfd)
{
    pfd[0] = STDIN_FILENO;
    pfd[1] = STDOUT_FILENO;
}

struct mustache_arg {
    flux_shell_t *shell;
    flux_shell_task_t *task;
    int shell_rank;
};

static char *do_mustache_render (flux_shell_t *shell,
                                 int shell_rank,
                                 flux_shell_task_t *task,
                                 const char *fmt)
{
    if (!shell) {
        /* Note: shell->mr and fmt checked in mustache_render */
        errno = EINVAL;
        return NULL;
    }
    /* Note: shell_rank >= shell_size is allowed so a caller can leave
     * node-specific tags unrendered in the result.
     */
    if (shell_rank < 0)
        shell_rank  = shell->info->shell_rank;
    if (task == NULL)
        task = shell->current_task;
    struct mustache_arg arg = {
        .shell = shell,
        .task = task,
        .shell_rank = shell_rank
    };
    return mustache_render (shell->mr, fmt, &arg);
}

char *flux_shell_rank_mustache_render (flux_shell_t *shell,
                                       int shell_rank,
                                       const char *fmt)
{
    return do_mustache_render (shell, shell_rank, NULL, fmt);
}

char *flux_shell_task_mustache_render (flux_shell_t *shell,
                                       flux_shell_task_t *task,
                                       const char *fmt)
{
    return do_mustache_render (shell, -1, task, fmt);
}

char *flux_shell_mustache_render (flux_shell_t *shell, const char *fmt)
{
    return do_mustache_render (shell, -1, NULL, fmt);
}

/* Render "node.*" specific tags using the rank_info object for the
 * requested shell rank. The part after `node.` will be fetched directly
 * from the rank_info object.
 */
static int mustache_render_node (flux_shell_t *shell,
                                 int shell_rank,
                                 const char *name,
                                 FILE *fp)
{
    int rc = -1;
    const char *s;
    char buf[24];
    json_t *o;
    json_t *val;

    if (!(o = flux_shell_get_rank_info_object (shell, shell_rank)))
        return -1;

    /* forward past "node." */
    s = name + 5;

    /* Special case: allow node.{cores,gpus,...} as shorthand for
     * node.resources.{cores,gpus,...}
     */
    if (streq (s, "cores") || streq (s, "gpus") || streq (s, "ncores")) {
        (void) snprintf (buf, sizeof (buf), "resources.%s", s);
        s = buf;
    }
    if ((val = jpath_get (o, s))) {
        if (json_is_string (val))
            rc = fputs (json_string_value (val), fp);
        else if (json_is_integer (val))
            rc = fprintf (fp, "%jd", (intmax_t) json_integer_value (val));
        else {
            /* Not expected, but template could be {{node.resources}}, handle
             * that here by dumping the JSON to fp
             */
            rc = json_dumpf (val, fp, JSON_COMPACT|JSON_ENCODE_ANY);
        }
    }
    else {
        errno = ENOENT;
        return -1;
    }
    if (rc < 0)
        shell_log_errno ("memstream write failed for %s", name);
    return rc;

}

/*  Render the following task-specific mustache templates for `task`
 *   {{task.id}}, {{task.rank}} - global task rank
 *   {{task.index}}, {{task.localid}} - local task index
 */
static int mustache_render_task (flux_shell_t *shell,
                                 flux_shell_task_t *task,
                                 const char *name,
                                 FILE *fp)
{
    const char *s;
    int value;

    if (!task) {
        /* Possibly a current task was not available at this time.
         * Return ENOENT so caller can handle errors
         */
        errno = ENOENT;
        return -1;
    }

    /* forward past `task.` */
    s = name + 5;
    if (streq (s, "id") || streq (s, "rank"))
        value = task->rank;
    else if (streq (s, "index") || streq (s, "localid"))
        value = task->index;
    else {
        errno = ENOENT;
        return -1;
    }
    if (fprintf (fp, "%d", value) < 0) {
        shell_log_errno ("memstream write failed for %s", name);
        return -1;
    }
    return 0;
}

static int mustache_render_name (flux_shell_t *shell,
                                 const char *name,
                                 FILE *fp)
{
    const char *jobname = NULL;
    json_error_t error;
    if (json_unpack_ex (shell->info->jobspec->jobspec,
                        &error,
                        0,
                        "{s:{s:{s?{s?s}}}}",
                        "attributes",
                         "system",
                          "job",
                           "name", &jobname) < 0) {
        shell_log_error ("render_name: %s", error.text);
        jobname = NULL;
    }
    if (!jobname) {
        json_t *cmd = json_array_get (shell->info->jobspec->command, 0);
        if (!cmd
            || !(jobname = json_string_value (cmd))
            || !(jobname = basename_simple (jobname)))
            jobname = "unknown";
    }
    if (fputs (jobname, fp) == EOF) {
        shell_log_error ("memstream write failed for %s: %s",
                         name,
                         strerror (errno));
    }
    return 0;
}

static int mustache_render_jobid (flux_shell_t *shell,
                                  const char *name,
                                  FILE *fp)
{
    char value[128];
    const char *type = "f58";

    if (strlen (name) > 2) {
        if (name[2] != '.') {
            errno = ENOENT;
            return -1;
        }
        type = name+3;
    }
    if (flux_job_id_encode (shell->info->jobid,
                            type,
                            value,
                            sizeof (value)) < 0) {
        if (errno == EPROTO)
            shell_log_error ("Invalid jobid encoding '%s' specified", name);
        else
            shell_log_errno ("flux_job_id_encode failed for %s", name);
        return -1;
    }
    if (fputs (value, fp) < 0) {
        shell_log_error ("memstream write failed for %s: %s",
                         name,
                         strerror (errno));
    }
    return 0;
}

static int mustache_cb (FILE *fp, const char *name, void *arg)
{
    int rc = -1;
    flux_plugin_arg_t *args;
    struct mustache_arg *m_arg = arg;
    const char *result = NULL;
    char topic[128];

    flux_shell_t *shell = m_arg->shell;

    /*  "jobid" is a synonym for "id" */
    if (strstarts (name, "jobid"))
        name += 3;
    /*  "taskid" is a synonym for "task.id" */
    else if (streq (name, "taskid"))
        name = "task.id";

    if (strstarts (name, "id"))
        return mustache_render_jobid (shell, name, fp);
    if (streq (name, "name"))
        return mustache_render_name (shell, name, fp);
    if (streq (name, "nnodes"))
        return fprintf (fp, "%d", shell->info->shell_size);
    if (streq (name, "ntasks") || streq (name, "size"))
        return fprintf (fp, "%d", shell->info->total_ntasks);
    if (strstarts (name, "task."))
        return mustache_render_task (shell, m_arg->task, name, fp);
    if (strstarts (name, "node."))
        return mustache_render_node (shell, m_arg->shell_rank, name, fp);

    if (snprintf (topic,
                  sizeof (topic),
                  "mustache.render.%s",
                  name) >= sizeof (topic)) {
        shell_log_error ("mustache template name '%s' too long", name);
        return -1;
    }
    if (!(args = flux_plugin_arg_create ())) {
        shell_log_error ("mustache_cb: failed to create plugin args");
        return -1;
    }
    if (plugstack_call (shell->plugstack, topic, args) < 0) {
        shell_log_errno ("%s", topic);
        goto out;
    }
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "result", &result) < 0
        || result == NULL) {
        errno = ENOENT;
        goto out;
    }
    if (fputs (result, fp) < 0) {
        shell_log_error ("memstream write failed for %s: %s",
                         name,
                         strerror (errno));
    }
    rc = 0;
out:
    flux_plugin_arg_destroy (args);
    return rc;
}

static void shell_initialize (flux_shell_t *shell)
{
    const char *pluginpath = shell_conf_get ("shell_pluginpath");

    memset (shell, 0, sizeof (struct flux_shell));

    if (gethostname (shell->hostname, sizeof (shell->hostname)) < 0)
        shell_die_errno (1, "gethostname");

    get_protocol_fd (shell->protocol_fd);

    if (!(shell->completion_refs = zhashx_new ()))
        shell_die_errno (1, "zhashx_new");
    zhashx_set_destructor (shell->completion_refs, item_free);

    if (!(shell->mr = mustache_renderer_create (mustache_cb)))
        shell_die_errno (1, "mustache_renderer_create");
    mustache_renderer_set_log (shell->mr, shell_llog, NULL);

    if (!(shell->plugstack = plugstack_create ()))
        shell_die_errno (1, "plugstack_create");

    if (plugstack_plugin_aux_set (shell->plugstack, "flux::shell", shell) < 0)
        shell_die_errno (1, "plugstack_plugin_aux_set");

    if (plugstack_set_searchpath (shell->plugstack, pluginpath) < 0)
        shell_die_errno (1, "plugstack_set_searchpath");

    if (shell_load_builtins (shell) < 0)
        shell_die_errno (1, "shell_load_builtins");
}

void flux_shell_killall (flux_shell_t *shell, int signum)
{
    struct shell_task *task;
    if (!shell ||  signum <= 0 || !shell->tasks)
        return;
    task = zlist_first (shell->tasks);
    while (task) {
        if (shell_task_running (task) && shell_task_kill (task, signum) < 0)
            shell_log_errno ("kill task %d: signal %d", task->rank, signum);
        task = zlist_next (shell->tasks);
    }
}

int flux_shell_add_completion_ref (flux_shell_t *shell,
                                   const char *fmt, ...)
{
    int rc = -1;
    int *intp = NULL;
    char *ref = NULL;
    va_list ap;

    if (!shell || !fmt) {
        errno = EINVAL;
        return -1;
    }

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0)
        goto out;
    if (!(intp = zhashx_lookup (shell->completion_refs, ref))) {
        if (!(intp = calloc (1, sizeof (*intp))))
            goto out;
        if (zhashx_insert (shell->completion_refs, ref, intp) < 0) {
            free (intp);
            goto out;
        }
    }
    rc = ++(*intp);
out:
    free (ref);
    va_end (ap);
    return (rc);

}

int flux_shell_remove_completion_ref (flux_shell_t *shell,
                                      const char *fmt, ...)
{
    int rc = -1;
    int *intp;
    va_list ap;
    char *ref = NULL;

    if (!shell || !fmt) {
        errno = EINVAL;
        return -1;
    }

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0)
        goto out;
    if (!(intp = zhashx_lookup (shell->completion_refs, ref))) {
        errno = ENOENT;
        goto out;
    }
    if (--(*intp) == 0) {
        zhashx_delete (shell->completion_refs, ref);
        if (zhashx_size (shell->completion_refs) == 0)
            flux_reactor_stop (shell->r);
    }
    rc = 0;
out:
    free (ref);
    va_end (ap);
    return (rc);
}

/*  Public shell interface to request additional context in one of
 *   the emitted shell events.
 */
int flux_shell_add_event_context (flux_shell_t *shell,
                                  const char *name,
                                  int flags,
                                  const char *fmt,
                                  ...)
{
    va_list ap;
    if (!shell || !name || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    int rc = shell_eventlogger_context_vpack (shell->ev, name, 0, fmt, ap);
    va_end (ap);
    return rc;
}

static int shell_barrier (flux_shell_t *shell, const char *name)
{
    char buf [8];

    if (shell->info->shell_size == 1)
        return 0; // NO-OP

    if (dprintf (shell->protocol_fd[1], "enter\n") != 6)
        shell_die_errno (1, "shell_barrier: dprintf");

    /*  Note: The only expected values currently are "exit=0\n"
     *   for success and "exit=1\n" for failure. Therefore, if
     *   read(2) fails, or we don't receive exactly "exit=0\n",
     *   then this barrier has failed. We exit immediately since
     *   the reason for the failed barrier has likely been logged
     *   elsewhere.
     */
    memset (buf, 0, sizeof (buf));
    if (read (shell->protocol_fd[0], buf, 7) < 0)
        shell_die_errno (1, "shell_barrier: read");
    if (!streq (buf, "exit=0\n"))
        exit (1);
    return 0;
}

static int load_initrc (flux_shell_t *shell, const char *default_rcfile)
{
    bool required = false;
    const char *rcfile = NULL;

    /* If initrc is set on command line or in jobspec, then
     *  it is required, O/w initrc is treated as empty file.
     */
    if (flux_shell_getopt_unpack (shell, "initrc", "s", &rcfile) > 0)
        required = true;
    else
        rcfile = default_rcfile;

    /* Skip loading initrc file if it is not required or the file isn't
     * readable.
     */
    if (!required && access (rcfile, R_OK) < 0)
        return 0;

    shell_debug ("Loading %s", rcfile);

    if (shell_rc (shell, rcfile) < 0) {
        shell_die (1,
                   "loading rc file %s%s%s",
                   rcfile,
                   errno ? ": " : "",
                   errno ? strerror (errno) : "");
        return -1;
    }

    return 0;
}

static int shell_initrc (flux_shell_t *shell)
{
    const char *default_rcfile = shell_conf_get ("shell_initrc");
    const char *result;

    if ((result = flux_attr_get (shell->h, "conf.shell_pluginpath")))
        plugstack_set_searchpath (shell->plugstack, result);
    if ((result = flux_attr_get (shell->h, "conf.shell_initrc")))
        default_rcfile = result;

    /* Change current working directory once before all tasks are
     * created, so that each task does not need to chdir().
     */
    if (shell->info->jobspec->cwd) {
        if (chdir (shell->info->jobspec->cwd) < 0) {
            shell_log_error ("host %s: Could not change dir to %s: %s. "
                             "Going to /tmp instead",
                             shell->hostname,
                             shell->info->jobspec->cwd,
                             strerror (errno));
            if (chdir ("/tmp") < 0) {
                shell_log_errno ("Could not change dir to /tmp");
                return -1;
            }
        }
    }

    /*  Load initrc file if necessary
     */
    return load_initrc (shell, default_rcfile);
}

static int shell_taskmap (flux_shell_t *shell)
{
    int rc;
    const char *scheme;
    const char *value = "";
    char *topic = NULL;
    char *map = NULL;
    char *newmap = NULL;
    flux_plugin_arg_t *args = NULL;
    struct taskmap *taskmap;
    flux_error_t error;

    if ((rc = flux_shell_getopt_unpack (shell,
                                       "taskmap",
                                       "{s:s s?s}",
                                       "scheme", &scheme,
                                       "value", &value)) < 0) {
        shell_log_error ("failed to parse taskmap shell option");
        return -1;
    }
    if (rc == 0)
        return 0;
    if (streq (scheme, "block")) {
        /*  A value is not allowed for the block taskmap scheme:
         */
        if (strlen (value) > 0)
            shell_die (1,
                       "block taskmap does not accept a value (got %s)",
                       value);
        return 0;
    }

    shell_trace ("remapping tasks with scheme=%s value=%s",
                 scheme,
                 value);

    if (streq (scheme, "manual")) {
        if (!(taskmap = taskmap_decode (value, &error)))
            shell_die (1, "taskmap=%s: %s", value, error.text);
        if (shell_info_set_taskmap (shell->info, taskmap) < 0)
            shell_die (1, "failed to set new shell taskmap");
        return 0;
    }

    rc = -1;
    if (!(map = taskmap_encode (shell->info->taskmap,
                                TASKMAP_ENCODE_WRAPPED))) {
        shell_log_errno ("taskmap.%s: taskmap_encode", scheme);
        return -1;
    }
    if (!(args = flux_plugin_arg_create ())
        || flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_IN,
                                 "{s:s s:s s:s}",
                                 "taskmap", map,
                                 "scheme", scheme,
                                 "value", value) < 0) {
        shell_log_error ("taskmap.%s: failed to create plugin args: %s",
                         scheme,
                         flux_plugin_arg_strerror (args));
        goto out;
    }
    if (asprintf (&topic, "taskmap.%s", scheme) < 0
        || plugstack_call (shell->plugstack, topic, args) < 0) {
        shell_log_errno ("%s failed", topic);
        goto out;
    }
    /*  Unpack arguments to get new taskmap  */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "taskmap", &newmap) < 0
        || newmap == NULL) {
        shell_die (1, "failed to map tasks with scheme=%s", scheme);
    }
    if (!(taskmap = taskmap_decode (newmap, &error))) {
        shell_log_error ("taskmap.%s returned invalid map: %s",
                          scheme,
                          error.text);
        goto out;
    }
    if (shell_info_set_taskmap (shell->info, taskmap) < 0) {
        shell_log_errno ("unable to update taskmap");
        goto out;
    }
    if (shell->info->shell_rank == 0)
        shell_debug ("taskmap uptdated to %s", newmap);
    rc = 0;
out:
    flux_plugin_arg_destroy (args);
    free (topic);
    free (map);
    return rc;
}

static int shell_init (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "shell.init", NULL);
}

static int shell_post_init (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "shell.post-init", NULL);
}

static int shell_task_init (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "task.init", NULL);
}

#if CODE_COVERAGE_ENABLED
extern void __gcov_dump ();
extern void __gcov_reset ();
#endif
static void shell_task_exec (flux_shell_task_t *task, void *arg)
{
    flux_shell_t *shell = arg;
    shell->current_task->in_pre_exec = true;

    /*  Set stdout to unbuffered so that any output from task.exec plugins
     *   is not lost at exec(2).
     */
    (void) setvbuf (stdout, NULL, _IONBF, 0);

    if (plugstack_call (shell->plugstack, "task.exec", NULL) < 0)
        shell_log_errno ("task.exec plugin(s) failed");
#if CODE_COVERAGE_ENABLED
    __gcov_dump ();
    __gcov_reset ();
#endif
}

static int shell_task_forked (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "task.fork", NULL);
}

static int shell_start (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "shell.start", NULL);
}

static int shell_exit (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "shell.exit", NULL);
}

/*  Log basic shell info at startup.
 */
static void shell_log_info (flux_shell_t *shell)
{
    if (shell->verbose) {
        struct shell_info *info = shell->info;
        char *taskids = idset_encode (info->taskids,
                                      IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS);

        if (info->shell_rank == 0)
            shell_debug ("0: task_count=%d slot_count=%d "
                         "cores_per_slot=%d slots_per_node=%d",
                         info->total_ntasks,
                         info->jobspec->slot_count,
                         info->jobspec->cores_per_slot,
                         info->jobspec->slots_per_node);
        shell_debug ("%d: task%s %s on cores %s",
                     info->shell_rank,
                     idset_count (info->taskids) > 1 ? "s" : "",
                     taskids ? taskids : "[unknown]",
                     info->rankinfo.cores);
        free (taskids);
    }
}

/*  Add default event context for standard shell emitted events -
 *   shell.init and shell.start.
 */
static int shell_register_event_context (flux_shell_t *shell)
{
    int rc = -1;
    json_t *o = NULL;
    if (shell->info->shell_rank != 0)
        return 0;
    o = taskmap_encode_json (shell->info->taskmap, TASKMAP_ENCODE_WRAPPED);
    if (o == NULL
        || flux_shell_add_event_context (shell,
                                         "shell.init",
                                         0,
                                         "{s:i s:i}",
                                         "leader-rank",
                                         shell->info->rankinfo.rank,
                                         "size",
                                         shell->info->shell_size) < 0
        || flux_shell_add_event_context (shell,
                                         "shell.start",
                                         0,
                                         "{s:O}",
                                         "taskmap", o) < 0)
        goto out;
    rc = 0;
out:
    json_decref (o);
    return rc;
}

/*  Setup common environment for this job directly in the jobspec environment.
 *  Task-specific environment is setup in shell_task_create().
 */
static int shell_setup_environment (flux_shell_t *shell)
{
    const char *uri;
    const char *namespace;

    (void) flux_shell_unsetenv (shell, "FLUX_PROXY_REMOTE");

    if (!(uri = getenv ("FLUX_URI"))
        || !(namespace = getenv ("FLUX_KVS_NAMESPACE"))
        || flux_shell_setenvf (shell, 1, "FLUX_URI", "%s", uri) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "FLUX_KVS_NAMESPACE",
                               "%s",
                               namespace) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "FLUX_JOB_SIZE",
                               "%d",
                               shell->info->total_ntasks) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "FLUX_JOB_NNODES",
                               "%d",
                               shell->info->shell_size) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "FLUX_JOB_ID",
                               "%s",
                               idf58 (shell->info->jobid)) < 0)
        return -1;

    /* If HOSTNAME is set in job environment it is almost certain to be
     * incorrect. Overwrite with the correct hostname.
     */
    if (flux_shell_getenv (shell, "HOSTNAME")
        && flux_shell_setenvf (shell,
                               1,
                               "HOSTNAME",
                               "%s",
                               shell->hostname) < 0)
        return -1;

    return 0;
}

/*  Export a static list of environment variables from the job environment
 *   to the current shell environment. This is important for variables like
 *   FLUX_F58_FORCE_ASCII which should influence some shell behavior.
 */
static int shell_export_environment_from_job (flux_shell_t *shell)
{
    const char *vars[] = {
        "FLUX_F58_FORCE_ASCII",
        NULL,
    };

    for (int i = 0; vars[i] != NULL; i++) {
        const char *val = flux_shell_getenv (shell, vars[i]);
        if (val && setenv (vars[i], val, 1) < 0)
            return shell_log_errno ("setenv (%s)", vars[i]);
    }
    return 0;
}

/*  Render any mustache templates that appear in command arguments.
 */
static int frob_command (flux_shell_t *shell, flux_cmd_t *cmd)
{
    for (int i = 0; i < flux_cmd_argc (cmd); i++) {
        if (strstr (flux_cmd_arg (cmd, i), "{{")) { // possibly mustachioed
            char *narg;
            if (!(narg = flux_shell_mustache_render (shell,
                                                     flux_cmd_arg (cmd, i)))
                || flux_cmd_argv_insert (cmd, i, narg) < 0) {
                free (narg);
                return -1;
            }
            free (narg);
            if (flux_cmd_argv_delete (cmd, i + 1) < 0)
                return -1;
        }
    }
    return 0;
}

static int shell_create_tasks (flux_shell_t *shell)
{
    int i = 0;
    int taskid;

    if (!(shell->tasks = zlist_new ()))
        shell_die (1, "zlist_new failed");

    taskid = idset_first (shell->info->taskids);
    while (taskid != IDSET_INVALID_ID) {
        struct shell_task *task;

        if (!(task = shell_task_create (shell, i, taskid)))
            shell_die (1, "shell_task_create index=%d", i);

        task->pre_exec_cb = shell_task_exec;
        task->pre_exec_arg = shell;

        if (zlist_append (shell->tasks, task) < 0)
            shell_die (1, "zlist_append failed");
        i++;
        taskid = idset_next (shell->info->taskids, taskid);
    }
    return 0;
}

static int shell_start_tasks (flux_shell_t *shell)
{
    flux_shell_task_t *task;

    task = zlist_first (shell->tasks);
    while (task) {
        shell->current_task = task;

        /*  Call all plugin task_init callbacks:
         */
        if (shell_task_init (shell) < 0)
            shell_die (1, "failed to initialize taskid=%d", task->rank);

        /*  Render any mustache templates in command args
         */
        if (frob_command (shell, task->cmd))
            shell_die (1, "failed rendering of mustachioed command args");

        if (shell_task_start (shell, task, task_completion_cb, shell) < 0) {
            int ec = 1;
            /* bash standard, 126 for permission/access denied, 127
             * for command not found.  Note that shell only launches
             * local tasks, therefore no need to check for
             * EHOSTUNREACH.
             */
            if (errno == EPERM || errno == EACCES)
                ec = 126;
            else if (errno == ENOENT)
                ec = 127;
            shell_die (ec,
                       "task %d (host %s): start failed: %s: %s",
                       task->rank,
                       shell->hostname,
                       flux_cmd_arg (task->cmd, 0),
                       strerror (errno));
        }

        if (flux_shell_add_completion_ref (shell, "task%d", task->rank) < 0)
            shell_die (1, "flux_shell_add_completion_ref");

        /*  Call all plugin task_fork callbacks:
         */
        if (shell_task_forked (shell) < 0)
            shell_die (1, "shell_task_forked");

        task = zlist_next (shell->tasks);
    }
    shell->current_task = NULL;
    return 0;
}

int main (int argc, char *argv[])
{
    flux_shell_t shell;

    memset (&shell, 0, sizeof (shell));

    /* Initialize locale from environment
     */
    setlocale (LC_ALL, "");

    shell_log_init (&shell, shell_name);

    shell_initialize (&shell);

    shell_parse_cmdline (&shell, argc, argv);

    /* Get reactor.
     */
    if (!(shell.r = flux_reactor_create (0)))
        shell_die_errno (1, "flux_reactor_create");

    /* Connect to broker:
     */
    shell_connect_flux (&shell);

    if (!(shell.ev = shell_eventlogger_create (&shell)))
        shell_die_errno (1, "shell_eventlogger_create");

    /* Subscribe to shell-<id>.* events. (no-op on loopback connector)
     */
    shell_events_subscribe (&shell);

    /* Populate 'struct shell_info' for general use by shell components.
     * Fetches missing info from shell handle if set.
     */
    if (!(shell.info = shell_info_create (&shell)))
        shell_die (1, "failed to initialize shell info");

    if (shell_export_environment_from_job (&shell) < 0)
        shell_die (1, "failed to initialize shell environment");

    /* Set verbose flag if set in attributes.system.shell.verbose */
    if (flux_shell_getopt_unpack (&shell, "verbose", "i", &shell.verbose) < 0)
        shell_die (1, "failed to parse attributes.system.shell.verbose");

    /* Set no_process_group if nosetpgrp option is set */
    if (flux_shell_getopt_unpack (&shell,
                                  "nosetpgrp",
                                  "i",
                                  &shell.nosetpgrp) < 0)
        shell_die (1, "failed to parse attributes.system.shell.nosetpgrp");

    /* Reinitialize log facility with new verbosity/shell.info */
    if (shell_log_reinit (&shell) < 0)
        shell_die_errno (1, "shell_log_reinit");

    /* Register service on the leader shell.
     */
    if (!(shell.svc = shell_svc_create (&shell)))
        shell_die (1, "shell_svc_create");

    /* Change working directory and Load shell initrc
     */
    if (shell_initrc (&shell) < 0)
        shell_die_errno (1, "shell_initrc");

    if (shell_taskmap (&shell) < 0)
        shell_die (1, "shell_taskmap");

    /* Register the default components of the shell.init eventlog event
     * context. This includes the current taskmap, which may have been
     * altered by a plugin during shell_initrc(), so this must be done
     * after that call completes.
     */
    if (shell_register_event_context (&shell) < 0)
        shell_die (1, "failed to add standard shell event context");

    /* Setup common environment for job.
     */
    if (shell_setup_environment (&shell) < 0)
        shell_die (1, "failed to setup common job environment");

    /* Create all tasks but do not start them. Tasks are started later
     * in shell_start_tasks().
     */
    if (shell_create_tasks (&shell) < 0)
        shell_die_errno (1, "shell_create_tasks");

    /* Call "shell_init" plugins.
     */
    if (shell_init (&shell) < 0)
        shell_die_errno (1, "shell_init");

    /* Now that verbosity, task mapping, etc. may have changed, log
     * basic shell info.
     */
    shell_log_info (&shell);

    /* Barrier to ensure initialization has completed across all shells.
     */
    if (shell_barrier (&shell, "init") < 0)
        shell_die_errno (1, "shell_barrier");

    /*  Emit an event after barrier completion from rank 0
     */
    if (shell.info->shell_rank == 0
        && shell_eventlogger_emit_event (shell.ev, "shell.init") < 0)
            shell_die_errno (1, "failed to emit event shell.init");

    /* Call shell.post-init plugins.
     */
    if (shell_post_init (&shell) < 0)
        shell_die_errno (1, "shell_post_init");

    /* Start all tasks
     */
    if (shell_start_tasks (&shell) < 0)
        shell_die (1, "shell_start_tasks failed");

    if (shell_start (&shell) < 0)
        shell_die_errno (1, "shell.start callback(s) failed");

    if (shell_barrier (&shell, "start") < 0)
        shell_die_errno (1, "shell_barrier");

    /*  Emit an event after barrier completion from rank 0
     */
    if (shell.info->shell_rank == 0
        && shell_eventlogger_emit_event (shell.ev, "shell.start") < 0)
            shell_die_errno (1, "failed to emit event shell.start");

    /* Main reactor loop
     * Exits when all completion references released
     */
    if (flux_reactor_run (shell.r, 0) < 0)
        shell_log_errno ("flux_reactor_run");

    if (shell_exit (&shell) < 0) {
        shell_log_error ("shell_exit callback(s) failed");
        /* Preset shell.rc to failure so failure here is ensured
         *  to cause shell to exit with non-zero exit code.
         * XXX: this should be replaced with more general fatal
         *  error function.
         */
        shell.rc = 1;
    }

    shell.rc = shell_max_task_exit (&shell);
    shell_debug ("exit %d", shell.rc);

    if (shell_rc_close ())
        shell_log_errno ("shell_rc_close");

    shell_finalize (&shell);

    /* Always close shell log after shell_finalize() in case shell
     *  components attempt to log during cleanup
     *  (e.g. plugin destructors)
     */
    shell_log_fini ();
    exit (shell.rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
