/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <flux/core.h>
#include <flux/optparse.h>
#include <signal.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/log.h"
#include "src/common/libsubprocess/fbuf.h"
#include "src/common/libsubprocess/fbuf_watcher.h"
#include "ccan/str/str.h"

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "IDSET",
      .usage = "Specify target ranks.  Default is \"all\"" },
    { .name = "exclude", .key = 'x', .has_arg = 1, .arginfo = "IDSET",
      .usage = "Exclude ranks from target." },
    { .name = "dir", .key = 'd', .has_arg = 1, .arginfo = "PATH",
      .usage = "Set the working directory to PATH" },
    { .name = "label-io", .key = 'l', .has_arg = 0,
      .usage = "Label lines of output with the source RANK" },
    { .name = "noinput", .key = 'n', .has_arg = 0,
      .usage = "Redirect stdin from /dev/null" },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Run with more verbosity." },
    { .name = "quiet", .key = 'q', .has_arg = 0,
      .usage = "Suppress extraneous output." },
    { .name = "service", .has_arg = 1, .arginfo = "NAME",
      .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Override service name (default: rexec)." },
    { .name = "setopt", .has_arg = 1, .arginfo = "NAME=VALUE",
      .flags = OPTPARSE_OPT_HIDDEN,
      .usage = "Set subprocess option NAME to VALUE (multiple use ok)" },
    { .name = "with-imp", .has_arg = 0,
      .usage = "Run args under 'flux-imp run'" },
    { .name = "jobid", .key = 'j', .has_arg = 1, .arginfo = "JOBID",
      .usage = "Set target ranks to nodes assigned to JOBID and  "
               "service name to job shell exec service" },
    OPTPARSE_TABLE_END
};

extern char **environ;

flux_t *flux_handle = NULL;
uint32_t rank_range;
uint32_t rank_count;
uint32_t started = 0;
uint32_t exited = 0;
int exit_code = 0;
zhashx_t *exitsets;
struct idset *hanging;

zlist_t *subprocesses;

optparse_t *opts = NULL;

int stdin_flags;
flux_watcher_t *stdin_w;

/* time to wait in between SIGINTs */
#define INTERRUPT_MILLISECS 1000.0

struct timespec last;
int sigint_count = 0;

bool use_imp = false;
const char *imp_path = NULL;

void output_exitsets (const char *key, void *item)
{
    struct idset *idset = item;
    int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;
    char *idset_str;

    if (!(idset_str = idset_encode (idset, flags)))
        log_err_exit ("idset_encode");

    /* key is string form of exit code / signal */
    fprintf (stderr, "%s: %s\n", idset_str, key);
    free (idset_str);
}

void idset_destroy_wrapper (void *data)
{
    struct idset *idset = data;
    idset_destroy (idset);
}

void completion_cb (flux_subprocess_t *p)
{
    int rank = flux_subprocess_rank (p);
    int ec, signum = 0;

    if ((ec = flux_subprocess_exit_code (p)) < 0) {
        /* bash standard, signals + 128 */
        if ((signum = flux_subprocess_signaled (p)) > 0)
            ec = signum + 128;
    }
    if (ec > exit_code)
        exit_code = ec;

    if (ec > 0) {
        char buf[128];
        struct idset *idset;

        if (signum)
            sprintf (buf, "%s", strsignal (signum));
        else
            sprintf (buf, "Exit %d", ec);

        /* use exit code as key for hash */
        if (!(idset = zhashx_lookup (exitsets, buf))) {
            if (!(idset = idset_create (rank_range, 0)))
                log_err_exit ("idset_create");
            (void)zhashx_insert (exitsets, buf, idset);
            (void)zhashx_freefn (exitsets, buf, idset_destroy_wrapper);
        }

        if (idset_set (idset, rank) < 0)
            log_err_exit ("idset_set");
    }

    if (idset_clear (hanging, rank) < 0)
        log_err_exit ("idset_clear");
}

void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state == FLUX_SUBPROCESS_RUNNING) {
        started++;
        /* see FLUX_SUBPROCESS_FAILED case below */
        (void)flux_subprocess_aux_set (p, "started", p, NULL);
    }
    else if (state == FLUX_SUBPROCESS_EXITED)
        exited++;
    else if (state == FLUX_SUBPROCESS_FAILED) {
        /* FLUX_SUBPROCESS_FAILED is a catch all error case, no way to
         * know if process started or not.  So we cheat with a
         * subprocess context setting.
         */
        if (flux_subprocess_aux_get (p, "started") == NULL)
            started++;
        exited++;
    }

    if (stdin_w) {
        if (started == rank_count)
            flux_watcher_start (stdin_w);
        if (exited == rank_count)
            flux_watcher_stop (stdin_w);
    }

    if (state == FLUX_SUBPROCESS_FAILED) {
        flux_cmd_t *cmd = flux_subprocess_get_cmd (p);
        int errnum = flux_subprocess_fail_errno (p);
        const char *errmsg = flux_subprocess_fail_error (p);
        int ec = 1;

        /* N.B. if no error message available from
         * flux_subprocess_fail_error(), errmsg is set to strerror of
         * subprocess errno.
         */
        log_msg ("Error: rank %d: %s: %s",
                 flux_subprocess_rank (p),
                 flux_cmd_arg (cmd, 0),
                 errmsg);

        /* bash standard, 126 for permission/access denied, 127 for
         * command not found.  68 (EX_NOHOST) for No route to host.
         */
        if (errnum == EPERM || errnum == EACCES)
            ec = 126;
        else if (errnum == ENOENT)
            ec = 127;
        else if (errnum == EHOSTUNREACH)
            ec = 68;

        if (ec > exit_code)
            exit_code = ec;
    }
}

void output_cb (flux_subprocess_t *p, const char *stream)
{
    FILE *fstream = streq (stream, "stderr") ? stderr : stdout;
    const char *ptr;
    int lenp;

    if (!(ptr = flux_subprocess_getline (p, stream, &lenp)))
        log_err_exit ("flux_subprocess_getline");

    if (lenp) {
        if (optparse_getopt (opts, "label-io", NULL) > 0)
            fprintf (fstream, "%d: ", flux_subprocess_rank (p));
        fwrite (ptr, lenp, 1, fstream);
    }
}

static void stdin_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct fbuf *fb = fbuf_read_watcher_get_buffer (w);
    flux_subprocess_t *p;
    const char *ptr;
    int lenp;

    if (!(ptr = fbuf_read (fb, -1, &lenp)))
        log_err_exit ("fbuf_read");

    if (lenp) {
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_state (p) == FLUX_SUBPROCESS_INIT
                || flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING) {
                if (flux_subprocess_write (p, "stdin", ptr, lenp) < 0)
                    log_err_exit ("flux_subprocess_write");
            }
            p = zlist_next (subprocesses);
        }
    }
    else {
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_close (p, "stdin") < 0)
                log_err_exit ("flux_subprocess_close");
            p = zlist_next (subprocesses);
        }
        flux_watcher_stop (stdin_w);
    }
}

static void kill_completion_cb (flux_subprocess_t *p)
{
    flux_subprocess_destroy (p);
}

static flux_subprocess_t *imp_kill (flux_subprocess_t *p, int signum)
{
    flux_cmd_t *cmd;
    flux_subprocess_ops_t ops = {
        .on_completion = kill_completion_cb,
        .on_state_change = NULL,
        .on_channel_out = NULL,
        .on_stdout = output_cb,
        .on_stderr = output_cb,
    };

    pid_t pid = flux_subprocess_pid (p);
    int rank = flux_subprocess_rank (p);

    if (!(cmd = flux_cmd_create (0, NULL, environ))
        || flux_cmd_argv_append (cmd, imp_path) < 0
        || flux_cmd_argv_append (cmd, "kill") < 0
        || flux_cmd_argv_appendf (cmd, "%d", signum) < 0
        || flux_cmd_argv_appendf (cmd, "-%ld", (long) pid) < 0) {
        fprintf (stderr,
                 "Failed to create flux-imp kill command for rank %d pid %d\n",
                 rank, pid);
        return NULL;
    }
    /* Note: subprocess object destroyed in completion callback
     */
    return flux_rexec (flux_handle, rank, 0, cmd, &ops);
}

static void killall (zlist_t *l, int signum)
{
    flux_subprocess_t *p = zlist_first (l);
    while (p) {
        if (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING) {
            if (use_imp) {
                if (!imp_kill (p, signum))
                    fprintf (stderr,
                             "failed to signal rank %d: %s\n",
                             flux_subprocess_rank (p),
                             strerror (errno));
            }
            else {
                flux_future_t *f = flux_subprocess_kill (p, signum);
                if (!f) {
                    if (optparse_getopt (opts, "verbose", NULL) > 0)
                        fprintf (stderr,
                                 "failed to signal rank %d: %s\n",
                                flux_subprocess_rank (p),
                                strerror (errno));
                }
                /* don't care about response */
                flux_future_destroy (f);
            }
        }
        p = zlist_next (l);
    }
}

static void signal_cb (int signum)
{
    if (signum == SIGINT) {
        if (sigint_count >= 2) {
            double since_last = monotime_since (last);
            if (since_last < INTERRUPT_MILLISECS) {
                int flags = IDSET_FLAG_BRACKETS | IDSET_FLAG_RANGE;
                char *idset_str;

                if (!(idset_str = idset_encode (hanging, flags)))
                    log_err_exit ("idset_encode");

                fprintf (stderr,
                         "%s: command still running at exit\n",
                         idset_str);
                free (idset_str);
                exit (1);
            }
        }
    }

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr,
                 "sending signal %d to %d running processes\n",
                 signum,
                 started - exited);

    killall (subprocesses, signum);

    if (signum == SIGINT) {
        if (sigint_count)
            fprintf (stderr,
                     "interrupt (Ctrl+C) one more time "
                     "within %.2f sec to exit\n",
                     (INTERRUPT_MILLISECS / 1000.0));

        monotime (&last);
        sigint_count++;
    }
}

void subprocess_destroy (void *arg)
{
    flux_subprocess_t *p = arg;
    flux_subprocess_destroy (p);
}

/* atexit handler
 * This is a good faith attempt to restore stdin flags to what they were
 * before we set O_NONBLOCK per bug #1803.
 */
void restore_stdin_flags (void)
{
    (void)fcntl (STDIN_FILENO, F_SETFL, stdin_flags);
}

char *split_opt (const char *s, char sep, const char **val)
{
    char *cpy = strdup (s);
    if (!cpy)
        return NULL;
    char *cp = strchr (cpy, sep);
    if (!cp) {
        free (cpy);
        errno = EINVAL;
        return NULL;
    }
    *cp++ = '\0';
    *val = cp;
    return cpy;
}

static bool check_for_imp_run (int argc, char *argv[], const char **ppath)
{
    /* If argv0 basename is flux-imp, then we'll likely have to use
     *  flux-imp kill to signal the resulting subprocesses
     */
    if (streq (basename (argv[0]), "flux-imp")) {
        *ppath = argv[0];
        return true;
    }
    return false;
}

static const char *get_flux_imp_path (flux_t *h)
{
    const char *imp = NULL;
    flux_future_t *f;

    if (!(f = flux_rpc (h, "config.get", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get_unpack (f,
                                "{s?{s?s}}",
                                "exec",
                                "imp", &imp) < 0)
        fprintf (stderr, "error fetching config object: %s",
                 future_strerror (f, errno));
    flux_aux_set (h, NULL, f, (flux_free_f) flux_future_destroy);
    return imp;
}

/*  Return true if all ids in `idset` are valid indices into `ranks`.
 */
static bool check_valid_indices (struct idset *ranks,
                                 struct idset *idset)
{
    /*  idset of NULL is valid since it will be treated as all ids
     */
    if (idset == NULL)
        return true;
    return (idset_last (idset) < idset_count (ranks));
}

static void filter_ranks (struct idset *ranks,
                          const char *include,
                          const char *exclude,
                          bool relative)
{
    unsigned int i;
    int n = 0;
    struct idset *include_ids = NULL;
    struct idset *exclude_ids = NULL;

    if (!streq (include, "all")
        && !(include_ids = idset_decode (include)))
        log_err_exit ("failed to decode idset '%s'", include);

    /*  include_ids is a set of indices into the `ranks` idset.
     *  (This works because we always start with ranks [0, size-1])
     *
     *  Check that each index in include_ids is valid before proceeding.
     */
    if (!check_valid_indices (ranks, include_ids))
        log_msg_exit ("One or more invalid --ranks specified: %s",
                      include);

    if (exclude && !(exclude_ids = idset_decode (exclude)))
        log_err_exit ("error decoding --exclude idset");

    /*  Note: it is not an error if exclude_ids falls outside of the
     *  ranks idset, this is simply ignored.
     */

    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        /*
         * Remove this id from ranks if one of the following is true
         *  - it is in exclude_ids if relative == false
         *  - the index of this id is in exclude_ids if relative == true
         *  - the index of this id is in include_ids if include_ids != NULL.
         */
        if (idset_test (exclude_ids, relative ? n : i)
            || (include_ids && !idset_test (include_ids, n))) {
            if (idset_clear (ranks, i) < 0)
                log_err_exit ("idset_clear");
        }
        i = idset_next (ranks, i);
        n++;
    }
    idset_destroy (include_ids);
    idset_destroy (exclude_ids);
}

/*  Get job shell rexec service name and broker ranks for job.
 */
int get_jobid_rexec_info (flux_t *h,
                          const char *jobid,
                          char **servicep,
                          struct idset **idsetp)
{
    flux_future_t *f;
    flux_jobid_t id;
    flux_job_state_t state;
    const char *ranks;
    struct idset *ids;
    bool done = false;

    if (flux_job_id_parse (jobid, &id) < 0)
        log_msg_exit ("error parsing jobid: \"%s\"", jobid);

    if (!(f = flux_rpc_pack (h,
                            "job-list.list-id",
                            FLUX_NODEID_ANY,
                            0,
                            "{s:I s:[ss]}",
                            "id", id,
                            "attrs", "ranks", "state"))
        || flux_rpc_get_unpack (f,
                                "{s:{s:i s:s}}",
                                "job",
                                 "state", &state,
                                 "ranks", &ranks) < 0) {
        if (errno == ENOENT)
            log_msg_exit ("job %s not found", jobid);
        log_err_exit ("unable to get info for job %s", jobid);
    }

    if (state != FLUX_JOB_STATE_RUN)
        log_msg_exit ("job %s is not currently running", jobid);

    if (!(ids = idset_decode (ranks)) || idset_empty (ids))
        log_msg_exit ("failed to get assigned ranks for %s", jobid);
    *idsetp = ids;

    flux_future_destroy (f);

    if (!(f = flux_job_event_watch (h,
                                    id,
                                    "guest.exec.eventlog",
                                    FLUX_JOB_EVENT_WATCH_WAITCREATE)))
        log_err_exit ("flux_job_event_watch");

    while (!done) {
        json_t *o;
        json_t *context;
        const char *event;
        const char *name;

        if (flux_job_event_watch_get (f, &event) < 0)
            log_msg_exit ("failed to get shell.init event for %s", jobid);

        if (!(o = eventlog_entry_decode (event))
            || eventlog_entry_parse (o, NULL, &name, &context) < 0)
            log_err_exit ("failed to decode exec eventlog event");

        if (streq (name, "shell.init")) {
            const char *service = NULL;
            if (json_unpack (context, "{s:s}", "service", &service) < 0)
                log_msg_exit ("failed to get service from shell.init event");
            if (asprintf (servicep, "%s.rexec", service) < 0)
                log_err_exit ("unable to create job rexec topic string");
            done = true;
        }
        json_decref (o);
        flux_future_reset (f);
    }
    flux_future_destroy (f);
    return 0;
}


int main (int argc, char *argv[])
{
    const char *optargp;
    int optindex;
    flux_reactor_t *r;
    struct idset *targets;
    uint32_t rank;
    flux_cmd_t *cmd;
    char *cwd = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_cb,
        .on_channel_out = NULL,
        .on_stdout = output_cb,
        .on_stderr = output_cb,
    };
    struct timespec t0;
    const char *service_name;
    char *job_service = NULL;

    log_init ("flux-exec");

    opts = optparse_create ("flux-exec");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    if (optindex == argc) {
        optparse_print_usage (opts);
        exit (1);
    }

    if (!(cmd = flux_cmd_create (argc - optindex, &argv[optindex], environ)))
        log_err_exit ("flux_cmd_create");

    flux_cmd_unsetenv (cmd, "FLUX_PROXY_REMOTE");

    if (optparse_getopt (opts, "dir", &optargp) > 0) {
        if (!(cwd = strdup (optargp)))
            log_err_exit ("strdup");
    }
    else {
        if (!(cwd = get_current_dir_name ()))
            log_err_exit ("get_current_dir_name");
    }

    if (!streq (cwd, "none")) {
        if (flux_cmd_setcwd (cmd, cwd) < 0)
            log_err_exit ("flux_cmd_setcwd");
    }
    if (optparse_hasopt (opts, "setopt")) {
        const char *arg;
        optparse_getopt_iterator_reset (opts, "setopt");
        while ((arg = optparse_getopt_next (opts, "setopt"))) {
            const char *value;
            char *name = split_opt (arg, '=', &value);
            if (!name || flux_cmd_setopt (cmd, name, value) < 0)
                log_err_exit ("error handling '%s' option", arg);
            free (name);
        }
    }

    if (!(flux_handle = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /* Assign h to flux_handle for local usage in main()
     */
    flux_t *h = flux_handle;

    if (!(r = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (flux_get_size (h, &rank_range) < 0)
        log_err_exit ("flux_get_size");

    if (optparse_hasopt (opts, "with-imp")) {
        if (!(imp_path = get_flux_imp_path (h)))
            log_err_exit ("--with-imp: exec.imp path not found in config");
        use_imp = true;
        if (flux_cmd_argv_insert (cmd, 0, "run") < 0
            || flux_cmd_argv_insert (cmd, 0, imp_path) < 0)
            log_err_exit ("failed to prepend 'flux-imp run' to command");
    }
    else {
        use_imp = check_for_imp_run (argc - optindex,
                                     &argv[optindex],
                                     &imp_path);
    }

    /* Allow systemd commands to work on flux systemd instance by
     * setting DBUS_SESSION_BUS_ADDRESS if not already set.
     * See flux-framework/flux-core#5901
     */
    const char *security_owner;
    if (!(security_owner = flux_attr_get (h, "security.owner")))
        log_err_exit ("failed to fetch security.owner attribute");
    (void)flux_cmd_setenvf (cmd,
                            0,
                            "DBUS_SESSION_BUS_ADDRESS",
                            "unix:path=/run/user/%s/bus",
                            security_owner);

    /* Get input ranks from --jobid if given:
     */
    if (optparse_getopt (opts, "jobid", &optargp) > 0) {
        get_jobid_rexec_info (h, optargp, &job_service, &targets);
    }
    else {
        if (!(targets = idset_create (0, IDSET_FLAG_AUTOGROW)))
            log_err_exit ("idset_create");
        if (idset_range_set (targets, 0, rank_range - 1) < 0)
            log_err_exit ("idset_range_set");
    }

    /* Include and exclude ranks based on --rank and --exclude options
     * Make rank exclusion relative to job ranks if --jobid was used.
     */
    filter_ranks (targets,
                  optparse_get_str (opts, "rank", "all"),
                  optparse_get_str (opts, "exclude", NULL),
                  optparse_hasopt (opts, "jobid"));

    rank_count = idset_count (targets);
    if (rank_count == 0)
        log_msg_exit ("No targets specified");
    if (!(hanging = idset_copy (targets)))
        log_err_exit ("idset_copy");

    monotime (&t0);
    if (optparse_getopt (opts, "verbose", NULL) > 0) {
        const char *argv0 = flux_cmd_arg (cmd, 0);
        char *nodeset = idset_encode (targets,
                                      IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS);
        if (!nodeset)
            log_err_exit ("idset_encode");
        fprintf (stderr,
                 "%03fms: Starting %s on %s\n",
                 monotime_since (t0),
                 argv0,
                 nodeset);
        free (nodeset);
    }

    if (!(subprocesses = zlist_new ()))
        log_err_exit ("zlist_new");

    if (!(exitsets = zhashx_new ()))
        log_err_exit ("zhashx_new()");

    service_name = optparse_get_str (opts,
                                     "service",
                                     job_service ? job_service : "rexec");
    rank = idset_first (targets);
    while (rank != IDSET_INVALID_ID) {
        flux_subprocess_t *p;
        if (!(p = flux_rexec_ex (h,
                                 service_name,
                                 rank,
                                 0,
                                 cmd,
                                 &ops,
                                 NULL,
                                 NULL)))
            log_err_exit ("flux_rexec");
        if (zlist_append (subprocesses, p) < 0)
            log_err_exit ("zlist_append");
        if (!zlist_freefn (subprocesses, p, subprocess_destroy, true))
            log_err_exit ("zlist_freefn");
        rank = idset_next (targets, rank);
    }

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr, "%03fms: Sent all requests\n", monotime_since (t0));

    /* -n,--noinput: close subprocess stdin
     */
    if (optparse_getopt (opts, "noinput", NULL) > 0) {
        flux_subprocess_t *p;
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_close (p, "stdin") < 0)
                log_err_exit ("flux_subprocess_close");
            p = zlist_next (subprocesses);
        }
    }
    /* configure stdin watcher
     */
    else {
        if ((stdin_flags = fcntl (STDIN_FILENO, F_GETFL)) < 0)
            log_err_exit ("fcntl F_GETFL stdin");
        if (atexit (restore_stdin_flags) != 0)
            log_err_exit ("atexit");
        if (fcntl (STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0)
            log_err_exit ("fcntl F_SETFL stdin");
        if (!(stdin_w = fbuf_read_watcher_create (r,
                                                  STDIN_FILENO,
                                                  1 << 20,
                                                  stdin_cb,
                                                  0,
                                                  NULL)))
            log_err_exit ("fbuf_read_watcher_create");
    }
    if (signal (SIGINT, signal_cb) == SIG_ERR)
        log_err_exit ("signal");

    if (signal (SIGTERM, signal_cb) == SIG_ERR)
        log_err_exit ("signal");

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr,
                 "%03fms: %d tasks complete with code %d\n",
                 monotime_since (t0),
                 exited,
                 exit_code);

    /* output message on any tasks that exited non-zero */
    if (!optparse_hasopt (opts, "quiet") && zhashx_size (exitsets) > 0) {
        struct id_set *idset = zhashx_first (exitsets);
        while (idset) {
            const char *key = zhashx_cursor (exitsets);
            output_exitsets (key, idset);
            idset = zhashx_next (exitsets);
        }
    }

    /* Clean up.
     */
    idset_destroy (targets);
    free (job_service);
    free (cwd);
    flux_cmd_destroy(cmd);
    flux_close (h);
    optparse_destroy (opts);
    log_fini ();

    zhashx_destroy (&exitsets);
    zlist_destroy (&subprocesses);

    return exit_code;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
