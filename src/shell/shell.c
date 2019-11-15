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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/log.h"

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
    { .name = "target-rank", .key = 'r', .has_arg = 1, .arginfo = "RANK",
      .usage = "Set execution target rank, otherwise use broker rank", },
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
    if (!shell->info->jobspec->options) {
        if (!(shell->info->jobspec->options = json_object ())) {
            errno = ENOMEM;
            return -1;
        }
    }
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
    if (json_str)
        *json_str = json_dumps (o, JSON_COMPACT|JSON_ENCODE_ANY);
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

    /* In standalone mode, jobspec, resources and target-rank must be
     *  set on command line:
     */
    if ((shell->standalone = optparse_hasopt (p, "standalone"))) {
        if (  !optparse_hasopt (p, "jobspec")
           || !optparse_hasopt (p, "resources")
           || !optparse_hasopt (p, "target-rank"))
            shell_die (1, "standalone mode requires --jobspec, "
                       "--resources and --target-rank");
    }

    if ((shell->verbose = optparse_getopt (p, "verbose", NULL)))
        shell_set_verbose (shell->verbose);
    shell->target_rank = optparse_get_int (p, "target-rank", -1);
    shell->p = p;
}

static void shell_connect_flux (flux_shell_t *shell)
{
    if (!(shell->h = flux_open (shell->standalone ? "loop://" : NULL, 0)))
        shell_die_errno (1, "flux_open");

    /*  Set reactor for flux handle to our custom created reactor.
     */
    flux_set_reactor (shell->h, shell->r);

    /*  Use broker rank for target rank if not set
     */
    if (shell->target_rank < 0) {
        uint32_t rank;
        if (flux_get_rank (shell->h, &rank) < 0)
            shell_log_errno ("error fetching broker rank");
        shell->target_rank = rank;
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
    if (!shell || !json_str) {
        errno = EINVAL;
        return -1;
    }
    *json_str = json_dumps (shell->info->jobspec->environment, JSON_COMPACT);
    return 0;
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
    if (!overwrite && json_object_get (env, name)) {
        errno = EEXIST;
        return -1;
    }

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc >= 0) {
        json_t *o = json_string (val);
        if (o)
            rc = json_object_set_new (env, name, o);
        free (val);
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

int flux_shell_get_info (flux_shell_t *shell, char **json_str)
{
    json_error_t err;
    json_t *o;
    if (!shell || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = json_pack_ex (&err, 0, "{ s:I s:i s:i s:i s:O s:{ s:i s:b }}",
                           "jobid", shell->info->jobid,
                           "rank",  shell->info->shell_rank,
                           "size",  shell->info->shell_size,
                           "ntasks", shell->info->rankinfo.ntasks,
                           "jobspec", shell->info->jobspec->jobspec,
                           "options",
                              "verbose", shell->verbose,
                              "standalone", shell->standalone)))
        return -1;
    *json_str = json_dumps (o, JSON_COMPACT);
    json_decref (o);
    return (*json_str ? 0 : -1);
}

int flux_shell_get_rank_info (flux_shell_t *shell,
                              int shell_rank,
                              char **json_str)
{
    struct rcalc_rankinfo ri;
    json_t *o = NULL;

    if (!shell || !json_str || shell_rank < -1) {
        errno = EINVAL;
        return -1;
    }

    if (shell_rank == -1)
        shell_rank = shell->info->shell_rank;

    if (rcalc_get_nth (shell->info->rcalc, shell_rank, &ri) < 0)
        return -1;

    o = json_pack ("{ s:i s:i s:{s:s s:s?}}",
                   "broker_rank", ri.rank,
                   "ntasks", ri.ntasks,
                   "resources",
                     "cores", shell->info->rankinfo.cores,
                     "gpus",  shell->info->rankinfo.gpus);
    if (!o)
        return -1;
    *json_str = json_dumps (o, JSON_COMPACT);
    json_decref (o);
    return (*json_str ? 0 : -1);
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

int flux_shell_service_register (flux_shell_t *shell,
                                 const char *method,
                                 flux_msg_handler_f cb,
                                 void *arg)
{
    if (!shell || !method || !cb) {
        errno = EINVAL;
        return -1;
    }
    return shell_svc_register (shell->svc, method, cb, arg);
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
    if (shell->tasks) {
        struct shell_task *task;
        while ((task = zlist_pop (shell->tasks)))
            shell_task_destroy (task);
        zlist_destroy (&shell->tasks);
    }
    aux_destroy (&shell->aux);

    plugstack_destroy (shell->plugstack);
    shell->plugstack = NULL;

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

static void shell_initialize (flux_shell_t *shell)
{
    const char *pluginpath = shell_conf_get ("shell_pluginpath");

    memset (shell, 0, sizeof (struct flux_shell));
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

static void eventlog_cb (flux_future_t *f, void *arg)
{
    json_t *o = NULL;
    json_t *context = NULL;
    const char *name;
    const char *entry;

    if (flux_job_event_watch_get (f, &entry) < 0) {
        if (errno == ENODATA)
            return;
        shell_log_errno ("flux_job_event_watch_get");
        return;
    }
    if (!(o = eventlog_entry_decode (entry))) {
        shell_log_errno ("eventlog_entry_decode: %s", entry);
        return;
    }
    if (eventlog_entry_parse (o, NULL, &name, &context) < 0) {
        shell_log_errno ("eventlog_entry_parse: %s", entry);
        return;
    }
    if (strcmp (name, "exception") == 0) {
        const char *type;
        int severity;
        const char *note = NULL;
        if (json_unpack (context, "{s:s s:i s?s}",
                         "type", &type,
                         "severity", &severity,
                         "note", &note) < 0) {
            shell_log_errno ("exception event unpack");
            return;
        }
        shell_log_set_exception_logged ();
        shell_die (1, "job.exception during init barrier, aborting");
    }
    json_decref (o);
    flux_future_reset (f);
}

static void barrier_cb (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    if (flux_future_get (f, NULL) < 0) {
        shell_log_errno ("flux_future_get: shell_barrier");
        flux_reactor_stop_error (r);
    }
    else {
        shell_trace ("shell barrier complete");
        flux_reactor_stop (r);
    }
}

static int shell_barrier (flux_shell_t *shell, const char *name)
{
    flux_future_t *log_f = NULL;
    flux_future_t *f = NULL;
    flux_t *h = NULL;
    flux_jobid_t id;
    char fqname[128];
    int rc = -1;

    if (shell->standalone || shell->info->shell_size == 1)
        return 0; // NO-OP
    id = shell->info->jobid;
    if (snprintf (fqname,
                  sizeof (fqname),
                  "shell-%ju-%s",
                  (uintmax_t) id,
                   name) >= sizeof (fqname)) {
        errno = EINVAL;
        return -1;
    }
    /*  Clone shell flux handle so that only barrier and eventlog watch
     *   messages are dispatched in the temporary reactor call here.
     *   This allows messages from other shell services to be requeued
     *   for the real reactor in main().
     */
    if (!(h = flux_clone (shell->h)))
        shell_die_errno (1, "flux_handle_clone");

    if (!(f = flux_barrier (h, fqname, shell->info->shell_size))) {
        shell_log_errno ("flux_barrier");
        goto out;
    }
    if (!(log_f = flux_job_event_watch (h, id, "eventlog", 0))) {
        shell_log_errno ("flux_job_event_watch");
        goto out;
    }
    if (flux_future_then (log_f, -1., eventlog_cb, NULL) < 0
        ||  flux_future_then (f, -1., barrier_cb, NULL) < 0) {
        shell_log_errno ("flux_future_then");
        goto out;
    }
    if (flux_future_then (f, -1., barrier_cb, shell) < 0) {
        shell_log_errno ("flux_future_then");
        goto out;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) >= 0)
        rc = 0;
    shell_trace ("exited barrier with rc = %d", rc);
out:
    flux_job_event_watch_cancel (log_f);
    flux_future_destroy (log_f);
    flux_future_destroy (f);

    /*  Close the cloned handle */
    flux_close (h);
    return rc;
}

static int shell_init (flux_shell_t *shell)
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
        rcfile = shell_conf_get ("shell_initrc");

    /*  Only try loading initrc if the file is readable or required:
     */
    if (access (rcfile, R_OK) == 0 || required) {
        shell_debug ("Loading %s", rcfile);

        if (shell_rc (shell, rcfile) < 0) {
            shell_die (1, "loading rc file %s%s%s",
                       rcfile,
                       errno ? ": " : "",
                       errno ? strerror (errno) : "");
        }
    }

    return plugstack_call (shell->plugstack, "shell.init", NULL);
}

static int shell_task_init (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "task.init", NULL);
}

static void shell_task_exec (flux_shell_task_t *task, void *arg)
{
    flux_shell_t *shell = arg;
    shell->current_task->in_pre_exec = true;
    if (plugstack_call (shell->plugstack, "task.exec", NULL) < 0)
        shell_log_errno ("task.exec plugin(s) failed");
}

static int shell_task_forked (flux_shell_t *shell)
{
    return plugstack_call (shell->plugstack, "task.fork", NULL);
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
                         info->jobspec->task_count,
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

int main (int argc, char *argv[])
{
    flux_shell_t shell;
    int i;

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

    /* Subscribe to shell-<id>.* events. (no-op on loopback connector)
     */
    shell_events_subscribe (&shell);

    /* Populate 'struct shell_info' for general use by shell components.
     * Fetches missing info from shell handle if set.
     */
    if (!(shell.info = shell_info_create (&shell)))
        exit (1);

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
        shell_die_errno (1, "shell_prepare");

    /* Barrier to ensure initialization has completed across all shells.
     */
    if (shell_barrier (&shell, "init") < 0)
        shell_die_errno (1, "shell_barrier");

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

        if (shell_task_start (task, shell.r, task_completion_cb, &shell) < 0)
            shell_die (1, "failed to start taskid=%d", i);

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

    shell_log_fini ();
    shell_finalize (&shell);
    exit (shell.rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
