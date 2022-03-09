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

#include "internal.h"
#include "builtins.h"
#include "info.h"
#include "svc.h"
#include "task.h"
#include "rc.h"
#include "log.h"

static char *shell_name = "flux-shell";
static const char *shell_usage = "[OPTIONS] JOBID";

static struct optparse_option shell_opts[] =  {
    { .name = "jobspec", .key = 'j', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get jobspec from FILE, not job-info service", },
    { .name = "resources", .key = 'R', .has_arg = 1, .arginfo = "FILE",
      .usage = "Get R from FILE, not job-info service", },
    { .name = "broker-rank", .key = 'r', .has_arg = 1, .arginfo = "RANK",
      .usage = "Set broker rank, rather than asking broker", },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Log actions to stderr", },
    { .name = "standalone", .key = 's', .has_arg = 0,
      .usage = "Run local program without Flux instance", },
    { .name = "initrc", .has_arg = 1, .arginfo = "FILE",
      .usage = "Load shell initrc from FILE instead of the system default" },
    OPTPARSE_TABLE_END
};

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

    /* In standalone mode, jobspec, resources and broker-rank must be
     *  set on command line:
     */
    if ((shell->standalone = optparse_hasopt (p, "standalone"))) {
        if (  !optparse_hasopt (p, "jobspec")
           || !optparse_hasopt (p, "resources")
           || !optparse_hasopt (p, "broker-rank"))
            shell_die (1, "standalone mode requires --jobspec, "
                       "--resources and --broker-rank");
    }

    if ((shell->verbose = optparse_getopt (p, "verbose", NULL)))
        shell_set_verbose (shell->verbose);
    shell->broker_rank = optparse_get_int (p, "broker-rank", -1);
    shell->p = p;
}

static void shell_connect_flux (flux_shell_t *shell)
{
    if (shell->standalone)
        shell->h = flux_open ("loop://", FLUX_O_TEST_NOSUB);
    else
        shell->h = flux_open (NULL, 0);
    if (!shell->h)
        shell_die_errno (1, "flux_open");

    /*  Set reactor for flux handle to our custom created reactor.
     */
    flux_set_reactor (shell->h, shell->r);

    /*  Fetch local rank if not already set
     */
    if (shell->broker_rank < 0) {
        uint32_t rank;
        if (flux_get_rank (shell->h, &rank) < 0)
            shell_log_errno ("error fetching broker rank");
        shell->broker_rank = rank;
    }
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

int flux_shell_setenvf (flux_shell_t *shell, int overwrite,
                        const char *name, const char *fmt, ...)
{
    json_t *env;
    va_list ap;
    char *val;
    int rc = -1;

    if (!shell || !name || !fmt) {
        errno = EINVAL;
        return -1;
    }

    env = shell->info->jobspec->environment;
    if (!overwrite && json_object_get (env, name))
        return 0;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc >= 0) {
        rc = object_set_string (env, name, val);
        ERRNO_SAFE_WRAP (free, val);
    }
    return rc;
}

int flux_shell_unsetenv (flux_shell_t *shell, const char *name)
{
    if (!shell || !name) {
        errno = EINVAL;
        return -1;
    }
    return json_object_del (shell->info->jobspec->environment, name);
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
                            "{ s:I s:i s:i s:i s:s s:O s:O s:{ s:i s:b }}",
                            "jobid", shell->info->jobid,
                            "rank",  shell->info->shell_rank,
                            "size",  shell->info->shell_size,
                            "ntasks", shell->info->total_ntasks,
                            "service", shell_svc_name (shell->svc),
                            "jobspec", shell->info->jobspec->jobspec,
                            "R", shell->info->R,
                            "options",
                               "verbose", shell->verbose,
                               "standalone", shell->standalone)))
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

static char *get_rank_task_idset (struct rcalc_rankinfo *ri)
{
    struct idset *ids;
    char *result = NULL;

    /*  Note: assumes taskids are always mapped using "block" allocation
     */
    int first = ri->global_basis;
    int last = ri->global_basis + ri->ntasks - 1;

    if (!(ids = idset_create (last+1, 0))
        || idset_range_set (ids, first, last) < 0)
        goto out;

    result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

static json_t *flux_shell_get_rank_info_object (flux_shell_t *shell, int rank)
{
    json_t *o;
    json_error_t error;
    char key [128];
    char *taskids = NULL;
    struct rcalc_rankinfo ri;

    if (!shell->info)
        return NULL;

    if (rank == -1)
        rank = shell->info->shell_rank;
    if (rcalc_get_nth (shell->info->rcalc, rank, &ri) < 0)
        return NULL;

    if (snprintf (key, sizeof (key), "shell::rinfo%d", rank) >= sizeof (key))
        return NULL;

    if ((o = flux_shell_aux_get (shell, key)))
        return o;

    if (!(taskids = get_rank_task_idset (&ri)))
        return NULL;

    o = json_pack_ex (&error, 0, "{ s:i s:i s:s s:{s:s s:s?}}",
                   "broker_rank", ri.rank,
                   "ntasks", ri.ntasks,
                   "taskids", taskids,
                   "resources",
                     "cores", shell->info->rankinfo.cores,
                     "gpus",  shell->info->rankinfo.gpus);
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

static int get_protocol_fd (int *pfd)
{
    const char *s;

    if ((s = getenv ("FLUX_EXEC_PROTOCOL_FD"))) {
        char *endptr;
        int fd;

        errno = 0;
        fd = strtol (s, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            errno = EINVAL;
            return -1;
        }
        if (fd_set_cloexec (fd) < 0)
            return -1;
        *pfd = fd;
        return 0;
    }
    *pfd = -1;
    return 0;
}

static void shell_initialize (flux_shell_t *shell)
{
    const char *pluginpath = shell_conf_get ("shell_pluginpath");

    memset (shell, 0, sizeof (struct flux_shell));

    if (gethostname (shell->hostname, sizeof (shell->hostname)) < 0)
        shell_die_errno (1, "gethostname");

    if (get_protocol_fd (&shell->protocol_fd) < 0)
        shell_die_errno (1, "Failed to parse FLUX_EXEC_PROTOCOL_FD");

    if (!(shell->completion_refs = zhashx_new ()))
        shell_die_errno (1, "zhashx_new");
    zhashx_set_destructor (shell->completion_refs, item_free);

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

    if (shell->standalone || shell->info->shell_size == 1)
        return 0; // NO-OP

    if (shell->protocol_fd < 0)
        shell_die (1, "required FLUX_EXEC_PROTOCOL_FD not set");

    if (dprintf (shell->protocol_fd, "enter\n") != 6)
        shell_die_errno (1, "shell_barrier: dprintf");

    /*  Note: The only expected values currently are "exit=0\n"
     *   for success and "exit=1\n" for failure. Therefore, if
     *   read(2) fails, or we don't receive exactly "exit=0\n",
     *   then this barrier has failed. We exit immediately since
     *   the reason for the failed barrier has likely been logged
     *   elsewhere.
     */
    memset (buf, 0, sizeof (buf));
    if (read (shell->protocol_fd, buf, 7) < 0)
        shell_die_errno (1, "shell_barrier: read");
    if (strcmp (buf, "exit=0\n") != 0)
        exit (1);
    return 0;
}

static int load_initrc (flux_shell_t *shell, const char *default_rcfile)
{
    bool required = false;
    const char *rcfile = NULL;

    /* If initrc is set on commmand line or in jobspec, then
     *  it is required, O/w initrc is treated as empty file.
     */
    if (optparse_getopt (shell->p, "initrc", &rcfile) > 0
        || flux_shell_getopt_unpack (shell, "initrc", "s", &rcfile) > 0)
        required = true;
    else
        rcfile = default_rcfile;

    /* Skip loading initrc file if it is not required and either the shell
     *  is running in standalone mode, or the file isn't readable.
     */
    if (!required && (shell->standalone || access (rcfile, R_OK) < 0))
        return 0;

    shell_debug ("Loading %s", rcfile);

    if (shell_rc (shell, rcfile) < 0) {
        shell_die (1, "loading rc file %s%s%s",
                   rcfile,
                   errno ? ": " : "",
                   errno ? strerror (errno) : "");
        return -1;
    }

    return 0;
}

static int shell_init (flux_shell_t *shell)
{
    const char *default_rcfile = shell_conf_get ("shell_initrc");

    /*  Override pluginpath, default rcfile from broker attribute
     *   if not in standalone mode.
     */
    if (!shell->standalone) {
        const char *result;
        if ((result = flux_attr_get (shell->h, "conf.shell_pluginpath")))
            plugstack_set_searchpath (shell->plugstack, result);
        if ((result = flux_attr_get (shell->h, "conf.shell_initrc")))
            default_rcfile = result;
    }

    /*  Load initrc file if necessary
     */
    if (load_initrc (shell, default_rcfile) < 0)
        return -1;

    /* Change current working directory once before all tasks are
     * created, so that each task does not need to chdir().
     */
    if (shell->info->jobspec->cwd) {
        if (chdir (shell->info->jobspec->cwd) < 0) {
            shell_log_error ("Could not change dir to %s: %s. "
                             "Going to /tmp instead",
                             shell->info->jobspec->cwd, strerror (errno));
            if (chdir ("/tmp") < 0) {
                shell_log_errno ("Could not change dir to /tmp");
                return -1;
            }
        }
    }

    return plugstack_call (shell->plugstack, "shell.init", NULL);
}

static int shell_task_init (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "task.init", NULL);
}

#if CODE_COVERAGE_ENABLED
extern void __gcov_flush ();
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
    __gcov_flush ();
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
        if (info->shell_rank == 0)
            shell_debug ("0: task_count=%d slot_count=%d "
                         "cores_per_slot=%d slots_per_node=%d",
                         info->total_ntasks,
                         info->jobspec->slot_count,
                         info->jobspec->cores_per_slot,
                         info->jobspec->slots_per_node);
        if (info->rankinfo.ntasks > 1)
            shell_debug ("%d: tasks [%d-%d] on cores %s",
                         info->shell_rank,
                         info->rankinfo.global_basis,
                         info->rankinfo.global_basis+info->rankinfo.ntasks - 1,
                         info->rankinfo.cores);
        else
            shell_debug ("%d: tasks [%d] on cores %s",
                         info->shell_rank,
                         info->rankinfo.global_basis,
                         info->rankinfo.cores);
    }
}

/*  Add default event context for standard shell emitted events -
 *   shell.init and shell.start.
 */
static int shell_register_event_context (flux_shell_t *shell)
{
    if (shell->standalone || shell->info->shell_rank != 0)
        return 0;
    if (flux_shell_add_event_context (shell, "shell.init", 0,
                                      "{s:i s:i}",
                                      "leader-rank",
                                      shell->info->rankinfo.rank,
                                      "size",
                                      shell->info->shell_size) < 0)
        return -1;
    if (flux_shell_add_event_context (shell, "shell.start", 0,
                                      "{s:i}",
                                      "task-count",
                                      shell->info->total_ntasks) < 0)
        return -1;
    return 0;
}

int main (int argc, char *argv[])
{
    flux_shell_t shell;
    int i;

    /* Initialize locale from environment
     */
    setlocale (LC_ALL, "");

    shell_log_init (&shell, shell_name);

    shell_initialize (&shell);

    shell_parse_cmdline (&shell, argc, argv);

    /* Get reactor capable of monitoring subprocesses.
     */
    if (!(shell.r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        shell_die_errno (1, "flux_reactor_create");

    /* Connect to broker, or if standalone, open loopback connector.
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
        exit (1);

    if (shell_register_event_context (&shell) < 0)
        shell_die (1, "failed to add standard shell event context");

    /* Set verbose flag if set in attributes.system.shell.verbose */
    if (flux_shell_getopt_unpack (&shell, "verbose", "i", &shell.verbose) < 0)
        shell_die (1, "failed to parse attributes.system.shell.verbose");

    /* Reinitialize log facility with new verbosity/shell.info */
    if (shell_log_reinit (&shell) < 0)
        shell_die_errno (1, "shell_log_reinit");

    /* Now that verbosity may have changed, log shell startup info */
    shell_log_info (&shell);

    /* Register service on the leader shell.
     */
    if (!(shell.svc = shell_svc_create (&shell)))
        shell_die (1, "shell_svc_create");

    /* Call shell initialization routines and "shell_init" plugins.
     */
    if (shell_init (&shell) < 0)
        shell_die_errno (1, "shell_init");

    /* Barrier to ensure initialization has completed across all shells.
     */
    if (shell_barrier (&shell, "init") < 0)
        shell_die_errno (1, "shell_barrier");

    /*  Emit an event after barrier completion from rank 0 if not in
     *   standalone mode.
     */
    if (shell.info->shell_rank == 0
        && !shell.standalone
        && shell_eventlogger_emit_event (shell.ev, 0, "shell.init") < 0)
            shell_die_errno (1, "failed to emit event shell.init");

    /* Create tasks
     */
    if (!(shell.tasks = zlist_new ()))
        shell_die (1, "zlist_new failed");
    for (i = 0; i < shell.info->rankinfo.ntasks; i++) {
        struct shell_task *task;

        if (!(task = shell_task_create (shell.info, i)))
            shell_die (1, "shell_task_create index=%d", i);

        task->pre_exec_cb = shell_task_exec;
        task->pre_exec_arg = &shell;
        shell.current_task = task;

        /*  Call all plugin task_init callbacks:
         */
        if (shell_task_init (&shell) < 0)
            shell_die (1, "failed to initialize taskid=%d", i);

        if (shell_task_start (task, shell.r, task_completion_cb, &shell) < 0) {
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
            shell_die (ec, "task %d: start failed: %s: %s",
                       task->rank,
                       flux_cmd_arg (task->cmd, 0),
                       strerror (errno));
        }

        if (zlist_append (shell.tasks, task) < 0)
            shell_die (1, "zlist_append failed");

        if (flux_shell_add_completion_ref (&shell, "task%d", task->rank) < 0)
            shell_die (1, "flux_shell_add_completion_ref");

        /*  Call all plugin task_fork callbacks:
         */
        if (shell_task_forked (&shell) < 0)
            shell_die (1, "shell_task_forked");
    }
    /*  Reset current task since we've left task-specific context:
     */
    shell.current_task = NULL;

    if (shell_start (&shell) < 0)
        shell_die_errno (1, "shell.start callback(s) failed");

    if (shell_barrier (&shell, "start") < 0)
        shell_die_errno (1, "shell_barrier");

    /*  Emit an event after barrier completion from rank 0 if not in
     *   standalone mode.
     */
    if (shell.info->shell_rank == 0
        && !shell.standalone
        && shell_eventlogger_emit_event (shell.ev, 0, "shell.start") < 0)
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
