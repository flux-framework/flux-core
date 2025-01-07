/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux-terminus.c - terminal session management service for Flux
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>

#include <jansson.h>

#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libutil/fdwalk.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/llog.h"

#include "src/common/libterminus/terminus.h"
#include "src/common/libterminus/pty.h"

#define TERMINUS_DOC "\
Simple terminal session manager and multiplexer for Flux.\n\
Options:\n"

int cmd_start (optparse_t *p, int argc, char **argv);
int cmd_attach (optparse_t *p, int argc, char **argv);
int cmd_list (optparse_t *p, int argc, char **argv);
int cmd_kill (optparse_t *p, int argc, char **argv);
int cmd_kill_server (optparse_t *p, int argc, char **argv);

static struct optparse_option global_opts[] =  {
    OPTPARSE_TABLE_END
};

static struct optparse_option start_opts[] = {
    { .name = "detach", .key = 'd',
      .usage = "Start new session and immediately detach"
    },
    { .name = "wait", .key = 'w',
      .usage = "Do not clear sessions from server on exit with --detach."
               " Instead, hold session in an 'exited' state until at least"
               " one client has attached."
    },
    { .name = "name", .key = 'n',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Set session name to NAME (default: arg0)",
    },
    { .name = "rank", .key = 'r',
      .has_arg = 1, .arginfo = "RANK",
      .usage = "Attach to session on rank RANK (default: local rank)",
    },
    { .name = "service", .key = 's',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Use service NAME (default USERID-terminus)."
    },
    { .name = "pipe", .key = 'p',
      .usage = "Pipe stdin to the session and exit. Do not display output",
      .flags = OPTPARSE_OPT_HIDDEN
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option attach_opts[] = {
    { .name = "rank", .key = 'r',
      .has_arg = 1, .arginfo = "RANK",
      .usage = "Attach to session on rank RANK (default: local rank)",
    },
    { .name = "service", .key = 's',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Attach at service NAME (default USERID-terminus)."
    },
    { .name = "pipe", .key = 'p',
      .usage = "Pipe stdin to the session and exit. Do not display output",
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option list_opts[] = {
    { .name = "rank", .key = 'r',
      .has_arg = 1, .arginfo = "RANK",
      .usage = "Attach to session on rank RANK (default: local rank)",
    },
    { .name = "service", .key = 's',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Use service NAME (default USERID-terminus)."
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option kill_opts[] = {
    { .name = "rank", .key = 'r',
      .has_arg = 1, .arginfo = "RANK",
      .usage = "Kill session on rank RANK (default: local rank)",
    },
    { .name = "service", .key = 's',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Kill at service NAME (default USERID-terminus). "
    },
    OPTPARSE_TABLE_END
};

static struct optparse_option kill_server_opts[] = {
    { .name = "rank", .key = 'r',
      .has_arg = 1, .arginfo = "RANK",
      .usage = "Kill server on rank RANK (default: local rank)",
    },
    { .name = "service", .key = 's',
      .has_arg = 1, .arginfo = "NAME",
      .usage = "Kill server at NAME (default USERID-terminus)."
    },
    OPTPARSE_TABLE_END
};


static struct optparse_subcommand subcommands[] = {
    { "start",
      "[OPTIONS] [COMMAND...]",
      "Start a new session",
      cmd_start,
      0,
      start_opts,
    },
    { "attach",
      "[OPTIONS] ID",
      "Attach to existing session",
      cmd_attach,
      0,
      attach_opts,
    },
    { "list",
      NULL,
      "list active sessions",
      cmd_list,
      0,
      list_opts,
    },
    { "kill",
      "[OPTIONS] ID",
      "kill active session ID",
      cmd_kill,
      0,
      kill_opts,
    },
     { "kill-server",
      NULL,
      "tell terminus server to exit",
      cmd_kill_server,
      0,
      kill_server_opts,
    },
    OPTPARSE_SUBCMD_END
};

int main (int argc, char *argv[])
{
    optparse_t *p;
    int optindex;
    int exitval;

    log_init ("flux-terminus");

    p = optparse_create ("flux-terminus");

    if (optparse_add_option_table (p, global_opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table() failed");

    if (optparse_add_doc (p, TERMINUS_DOC, 0) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_doc failed");

    if (optparse_reg_subcommands (p, subcommands) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_reg_subcommands");

    if ((optindex = optparse_parse_args (p, argc, argv)) < 0)
        exit (1);

    if ((argc - optindex == 0)
        || !optparse_get_subcommand (p, argv[optindex]))
        optparse_fatal_usage (p, 1, NULL);

    if ((exitval = optparse_run_subcommand (p, argc, argv)) < 0)
        exit (1);

    optparse_destroy (p);
    log_fini ();
    return (exitval);
}

char *service_name (optparse_t *p, const char *method,
                          char *buf, int buflen)
{
    char default_service [64];
    const char *service = NULL;

    if (optparse_getopt (p, "service", &service) <= 0) {
        /*  <userid>-terminus is guaranteed to fit in buffer:
         */
        (void) sprintf (default_service,
                        "%u-terminus",
                        (unsigned) getuid ());
        service = default_service;
    }
    if (snprintf (buf,
                  buflen,
                  "%s%s%s",
                  service,
                  method ? "." : "",
                  method ? method : "") >= buflen) {
        log_msg ("service_name: service name too long");
        return NULL;
    }
    return buf;
}

static void terminus_server_closefd (void *arg, int fd)
{
    int *savefd = arg;

    if (fd != *savefd
        && fd != STDIN_FILENO
        && fd != STDOUT_FILENO
        && fd != STDERR_FILENO)
        (void) close (fd);
}

static void close_stdio ()
{
    int fd = open ("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2 (fd, STDIN_FILENO);
        dup2 (fd, STDOUT_FILENO);
        dup2 (fd, STDERR_FILENO);
        close (fd);
    }
}

static void f_logf (void *arg,
                    const char *file,
                    int line,
                    const char *func,
                    const char *subsys,
                    int level,
                    const char *fmt,
                    va_list ap)
{
    flux_t *h = arg;
    char buf [2048];
    int buflen = sizeof (buf);
    int n = vsnprintf (buf, buflen, fmt, ap);
    if (n >= sizeof (buf)) {
        buf[buflen - 1] = '\0';
        buf[buflen - 1] = '+';
    }
    flux_log (h, level, "%s:%d: %s: %s", file, line, func, buf);
}

static void unregister_cb (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_err ("failed to unregister terminus service");
    flux_reactor_stop ((flux_reactor_t *) arg);
}

static void empty_cb (struct flux_terminus_server *ts,
                      void *arg)
{
    flux_future_t *f;
    if (!(f = flux_terminus_server_unregister (ts))
        || flux_future_then (f, -1., unregister_cb, arg) < 0)
        log_err_exit ("failed to unregister terminus service");
}

static int run_service (const char *service, int fd)
{
    flux_t *h;
    struct flux_terminus_server *ts;
    flux_future_t *f;
    int rc = -1;

    if (fdwalk (terminus_server_closefd, &fd) < 0) {
        log_err ("fdwalk");
        goto err;
    }

    if (!(h = flux_open (NULL, 0))) {
        log_err ("flux_open");
        goto err;
    }
    if (!(f = flux_service_register (h, service))
        || flux_future_get (f, NULL) < 0) {
        log_err ("flux_service_register (%s)", service);
        goto err;
    }
    flux_future_destroy (f);

    if (!(ts = flux_terminus_server_create (h, service))) {
        log_err ("flux_terminus_server_create");
        goto err;
    }

    /* Notify grandparent that we're ready */
    close (fd);
    close_stdio ();

    flux_terminus_server_set_log (ts, f_logf, h);

    /*  Set up to exit when the last session exits
     */
    flux_terminus_server_notify_empty (ts, empty_cb, flux_get_reactor (h));

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    flux_terminus_server_destroy (ts);
    flux_close (h);
    return rc;
err:
    if (write (fd, &errno, sizeof (int)) < 0)
        log_err ("write");
    return -1;
}

/*  Turn current process into daemon and start terminus server.
 *   Close write end of pfds when ready.
 */
static int start_service_daemon (flux_t *orig_h, optparse_t *p)
{
    pid_t pid;
    int pfds[2];
    int result = 0;
    char service [64];

    if (!service_name (p, NULL, service, sizeof (service)))
        log_msg_exit ("failed to get service name");

    /*  Create pipe to allow server to signal readiness */
    if (pipe (pfds) < 0)
        log_msg_exit ("pipe");

    if ((pid = fork ()) < 0)
        log_err_exit ("fork");
    else if (pid == 0) {
        /* Child: cleanup, fork again and execute server */
        flux_close (orig_h);
        close (pfds[0]);
        setsid ();
        if ((pid = fork ()) < 0)
            log_err_exit ("child: fork");
        else if (pid == 0) {
            /* Run server */
            if (run_service (service, pfds[1]) < 0)
                exit (1);
            exit (0);
        }
        /* Parent: exit */
        exit (0);
    }

    /*  Wait for child, wait for grandchild to close pipe */
    waitpid (pid, NULL, 0);
    close (pfds[1]);

    /*  Read status from grandchild */
    if (read (pfds[0], &result, sizeof (int)) < 0)
        log_err_exit ("Failed to get status of server");
    close (pfds[0]);

    errno = result;
    return result == 0 ? 0 : -1;
}

static json_t *build_cmd (optparse_t *p, int argc, char **argv)
{
    json_t *cmd = NULL;

    if (!(cmd = json_array ()))
        return NULL;

    for (int i = 0; i < argc; i++) {
        json_t *o = json_string (argv[i]);
        if (o == NULL)
            goto err;
        if (json_array_append_new (cmd, o) < 0) {
            json_decref (o);
            goto err;
        }
    }
    return cmd;
err:
    json_decref (cmd);
    return NULL;
}

static int new_session (flux_t *h, optparse_t *p,
                        const char *name,
                        int argc,
                        char **argv,
                        char **pty_service)
{
    int rc = -1;
    json_t *cmd = NULL;
    flux_future_t *f = NULL;
    char service [128];
    const char *s;
    int wait = 0;
    int rank = optparse_get_int (p, "rank", FLUX_NODEID_ANY);

    if (!(service_name (p, "new", service, sizeof (service))))
        goto err;
    cmd = build_cmd (p, argc, argv);

    if (!optparse_hasopt (p, "detach") || optparse_hasopt (p, "wait"))
        wait = 1;

    if (!(f = flux_rpc_pack (h,
                             service,
                             rank,
                             0,
                             "{s:s s:o? s:i}",
                             "name", name ? name : "",
                             "cmd", cmd,
                             "wait", wait)))
        goto err;
    if (flux_rpc_get_unpack (f,"{s:s}", "pty_service", &s) < 0) {
        log_err ("new session: %s", future_strerror (f, errno));
        goto err;
    }
    if (!(*pty_service = strdup (s)))
        goto err;
    rc = 0;
err:
    flux_future_destroy (f);
    return rc;
}

flux_future_t * list_sessions (flux_t *h, optparse_t *p)
{
    char service [128];
    int rank = optparse_get_int (p, "rank", FLUX_NODEID_ANY);

    if (!(service_name (p, "list", service, sizeof (service))))
        log_msg_exit ("Failed to build service name");

    return flux_rpc (h, service, NULL, rank, 0);
}

static void exit_cb (struct flux_pty_client *c, void *arg)
{
    flux_t *h = arg;
    flux_reactor_stop (flux_get_reactor (h));
}

static int attach_session (flux_t *h,
                           optparse_t *p,
                           const char *pty_service)
{
    int status = 0;
    int flags = FLUX_PTY_CLIENT_CLEAR_SCREEN
              | FLUX_PTY_CLIENT_NOTIFY_ON_DETACH
              | FLUX_PTY_CLIENT_ATTACH_SYNC;
    struct flux_pty_client *c = NULL;
    int rank = optparse_get_int (p, "rank", FLUX_NODEID_ANY);

    if (optparse_hasopt (p, "pipe")) {
        flags = FLUX_PTY_CLIENT_STDIN_PIPE
                | FLUX_PTY_CLIENT_ATTACH_SYNC
                | FLUX_PTY_CLIENT_NORAW;
    }

    if (!(c = flux_pty_client_create ())
        || flux_pty_client_set_flags (c, flags) < 0)
        log_err_exit ("flux_pty_client_create");

    if (flux_pty_client_attach (c, h, rank, pty_service) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("Invalid session or server at %s", pty_service);
        else
            log_err_exit ("flux_pty_client_attach");
    }

    if (flux_pty_client_notify_exit (c, exit_cb, h) < 0)
        log_msg_exit ("flux_pty_client_notify_exit");

    flux_reactor_run (flux_get_reactor (h), 0);

    if (flux_pty_client_exit_status (c, &status) < 0)
        log_err ("failed to get remote exit status");

    /* Exit with semi "standard" exit code (as shell might have) */
    if (status != 0) {
        int code = 1;
        if (WIFSIGNALED (status))
            code = 128 + WTERMSIG (status);
        else
            code = WEXITSTATUS (status);
        exit (code);
    }
    flux_pty_client_destroy (c);
    return 0;
}

/*  from flux-job.c
 */
static bool isnumber (const char *s, int *result)
{
    char *endptr;
    long int l;

    errno = 0;
    l = strtol (s, &endptr, 10);
    if (errno
        || *endptr != '\0'
        || l < 0) {
        return false;
    }
    *result = (int) l;
    return true;
}

int cmd_attach (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    const char *idstr;
    char service [128];
    int id;
    int optindex = optparse_option_index (p);

    if (getenv ("FLUX_TERMINUS_SESSION"))
        log_msg_exit ("Nesting flux-terminus sessions not supported");

    if (argc - optindex != 1)
        optparse_fatal_usage (p, 1, "session ID required\n");

    idstr = argv[optindex];
    if (!isnumber (idstr, &id))
        optparse_fatal_usage (p, 1, "session ID must be an integer\n");
    if (!service_name (p, idstr, service, sizeof (service)))
        log_msg_exit ("service_name");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (attach_session (h, p, service) < 0)
        log_msg_exit ("Failed to attach to session at %s", service);

    flux_close (h);
    return 0;
}

int cmd_start (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    json_t *o = NULL;
    const char *name = NULL;
    char *pty_service = NULL;
    flux_future_t *f = NULL;
    int optindex = optparse_option_index (p);

    if (getenv ("FLUX_TERMINUS_SESSION"))
        log_msg_exit ("Nesting flux-terminus sessions not supported");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    /*  If no server currently running at requested service endpoint,
     *   then start one in background on this rank
     */
    if (!(f = list_sessions (h, p))
        || flux_future_get (f, NULL) < 0) {
        if (optparse_hasopt (p, "rank"))
            log_msg_exit ("Unable to start a new server with --rank option");

        flux_future_destroy (f);
        f = NULL;

        /*  Fork service daemon. Only parent returns */
        if (start_service_daemon (h, p) < 0)
            log_msg_exit ("Failed to start a new server");
    }
    json_decref (o);

    argc -= optindex;
    argv += optindex;

    name = optparse_get_str (p, "name", NULL);
    if (new_session (h, p, name, argc, argv, &pty_service) < 0)
        log_msg_exit ("Failed to start new session");

    if (!optparse_hasopt (p, "detach")
        && attach_session (h, p, pty_service) < 0)
        log_msg_exit ("Failed to attach to session at %s", pty_service);

    free (pty_service);
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

static const char *timestr (double ts, char *buf, size_t size)
{
    struct tm tm;
    time_t sec = ts;
    if (!gmtime_r (&sec, &tm)
        || strftime (buf, size, "%c", &tm) == 0)
        return "Unknown";
    return buf;
}

static int print_session (json_t *o)
{
    int id;
    int clients;
    int exited;
    const char *name;
    double ctime;
    char timebuf [64];

    if (json_unpack (o, "{s:i s:i s:s s:i s:f}",
                     "id", &id,
                     "clients", &clients,
                     "name", &name,
                     "exited", &exited,
                     "ctime", &ctime) < 0)
        return -1;
    printf ("%d: [%s]%s %d clients (created %s)\n",
            id, name, exited ? " (exited)" : "",
            clients, timestr (ctime, timebuf, sizeof (timebuf)));
    return 0;
}

int cmd_list (optparse_t *p, int argc, char **argv)
{
    flux_future_t *f = NULL;
    json_t *l = NULL;
    const char *service;
    char datestr [64];
    int rank;
    size_t n;
    double ctime;
    flux_t *h;

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");
    if (!(f= list_sessions (h, p)))
        log_err_exit ("list_sessions");
    if (flux_rpc_get_unpack (f, "{s:o s:{s:s s:i s:f}}",
                             "sessions", &l,
                             "server",
                               "service", &service,
                               "rank", &rank,
                               "ctime", &ctime) < 0) {
        char name[64];
        if (errno == ENOSYS)
            log_msg_exit ("no server running at %s",
                          service_name (p, NULL, name, sizeof (name)));
        else
            log_err_exit ("list sessions failed");
    }
    timestr (ctime, datestr, sizeof (datestr));
    printf ("server at %s running on rank %d since %s\n",
            service,
            rank,
            datestr);
    if ((n = json_array_size (l)))
        printf ("%zu current session%s:\n", n, n > 1 ? "s" : "");
    else
        printf ("no sessions\n");
    for (int i = 0; i < json_array_size (l); i++)
        print_session (json_array_get (l, i));
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_kill (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    char service [128];
    const char *idstr;
    int id = -1;
    int optindex = optparse_option_index (p);
    int rank = optparse_get_int (p, "rank", FLUX_NODEID_ANY);

    if (argc - optindex != 1)
        optparse_fatal_usage (p, 1, "session ID required\n");

    idstr = argv[optindex];
    if (!isnumber (idstr, &id))
        optparse_fatal_usage (p, 1, "session ID must be an integer\n");
    if (!service_name (p, "kill", service, sizeof (service)))
        log_msg_exit ("service_name");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(f = flux_rpc_pack (h, service, rank, 0,
                             "{s:i s:i s:i}",
                             "id", id,
                             "signal", SIGKILL,
                             "wait", 1))
        || flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("kill: no server running at %s",
                          service_name (p, NULL, service, sizeof (service)));
        log_err_exit ("kill failed");
    }
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

int cmd_kill_server (optparse_t *p, int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    char service [64];
    int rank = optparse_get_int (p, "rank", FLUX_NODEID_ANY);
    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!service_name (p, "kill-server", service, sizeof (service)))
        log_err_exit ("failed to build service name");

    if (!(f = flux_rpc (h, service, NULL, rank, 0))
        || flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOSYS)
            log_msg_exit ("no server running at %s",
                          service_name (p, NULL, service, sizeof (service)));
        else
            log_err_exit ("kill-server");
    }
    flux_future_destroy (f);
    flux_close (h);
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
