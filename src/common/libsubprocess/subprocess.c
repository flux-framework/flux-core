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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <czmq.h>
#include <argz.h>
#include <envz.h>
#include <jansson.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/fdwalk.h"
#include "zio.h"
#include "subprocess.h"

#define FDA_LENGTH 16

struct subprocess_manager {
    zlist_t *processes;
    int wait_flags;
    flux_reactor_t *reactor;
};

struct subprocess {
    struct subprocess_manager *sm;
    zhash_t *zhash;

    pid_t pid;

    /* socketpair for synchronization */
    int parentfd;
    int childfd;

    char *cwd;

    size_t argz_len;
    char *argz;

    size_t envz_len;
    char *envz;

    int child_fda[FDA_LENGTH]; /* child end of subprocess_socketpair(s) */

    int refcount;
    int status;
    int exec_error;

    unsigned short started:1;
    unsigned short running:1;
    unsigned short exited:1;
    unsigned short completed:1;


    zio_t *zio_in;
    zio_t *zio_out;
    zio_t *zio_err;
    flux_watcher_t *child_watcher;

    subprocess_io_cb_f io_cb;

    zlist_t *hooks [SUBPROCESS_HOOK_COUNT];
};

static void subprocess_free (struct subprocess *p);


static void fda_zero (int fda[])
{
    int i;
    for (i = 0; i < FDA_LENGTH; i++)
        fda[i] = -1;
}

static int fda_set (int fda[], int fd)
{
    int i;
    for (i = 0; i < FDA_LENGTH; i++) {
        if (fda[i] == fd || fda[i] == -1) {
            fda[i] = fd;
            return 0;
        }
    }
    return -1;
}

static bool fda_isset (int fda[], int fd)
{
    int i;
    for (i = 0; i < FDA_LENGTH; i++) {
        if (fda[i] == fd)
            return true;
    }
    return false;
}

static void fda_closeall (int fda[])
{
    int i;
    for (i = 0; i < FDA_LENGTH; i++) {
        if (fda[i] != -1) {
            close (fda[i]);
            fda[i] = -1;
        }
    }
}

static int sigmask_unblock_all (void)
{
    sigset_t mask;
    sigemptyset (&mask);
    return sigprocmask (SIG_SETMASK, &mask, NULL);
}

static void subprocess_ref (struct subprocess *p)
{
    ++p->refcount;
}

static void subprocess_unref (struct subprocess *p)
{
    if (--p->refcount == 0)
        subprocess_free (p);
}

static int subprocess_run_hooks (struct subprocess *p, zlist_t *hooks)
{
    subprocess_cb_f fn = zlist_first (hooks);
    int rc = 0;
    subprocess_ref (p);
    while (fn) {
        if ((rc = fn (p)) < 0)
            goto done;
        fn = zlist_next (hooks);
    }
done:
    subprocess_unref (p);
    return (rc);
}

/*
 *  Default handler for stdout/err: send output directly into
 *   stdout/err of caller...
 */
static int send_output_to_stream (const char *name, const char *json_str)
{
    FILE *fp = stdout;
    char *s = NULL;
    bool eof;

    int len = zio_json_decode (json_str, (void **) &s, &eof);

    if (strcmp (name, "stderr") == 0)
        fp = stderr;

    if (len > 0)
        fputs (s, fp);
    if (eof)
        fclose (fp);

    free (s);
    return (len);
}

static int check_completion (struct subprocess *p)
{
    if (!p->started)
        return (0);
    //if (p->completed) /* completion event already sent */
     //   return (0);

    /*
     *  Check that all I/O is closed and subprocess has exited
     *   (and been reaped)
     */
    if (subprocess_io_complete (p) && subprocess_exited (p)) {
        p->completed = 1;
        return subprocess_run_hooks (p, p->hooks [SUBPROCESS_COMPLETE]);
    }
    return (0);
}

static int output_handler (zio_t *z, const char *json_str, int len, void *arg)
{
    struct subprocess *p = (struct subprocess *) arg;
    json_t *o = NULL;
    json_t *new_o;
    char *json_str_amended;

    if (p->io_cb) {
        if (!(o = json_loads (json_str, 0, NULL))) {
            errno = EINVAL;
            return -1;
        }
        if (!(new_o = json_pack ("{s:i s:s s:s}", "pid", subprocess_pid (p),
                                                  "type", "io",
                                                  "name", zio_name (z))))
            goto error;
        if (json_object_update (o, new_o) < 0) {
            json_decref (new_o);
            goto error;
        }
        json_decref (new_o);
        if (!(json_str_amended = json_dumps (o, JSON_COMPACT)))
            goto error;
        p->io_cb (p, json_str_amended);
        free (json_str_amended);
        json_decref (o);
    }
    else
       send_output_to_stream (zio_name (z), json_str);

    /*
     * Check for process completion in case EOF from I/O stream and process
     *  already registered exit
     */
    check_completion (p);
    return (0);
error:
    json_decref (o);
    errno = EINVAL;
    return -1;
}

static int hooks_table_init (struct subprocess *p)
{
    int i;
    for (i = 0; i < SUBPROCESS_HOOK_COUNT; i++) {
        if (!(p->hooks [i] = zlist_new ()))
            return (-1);
    }
    return (0);
}

static void hooks_table_free (struct subprocess *p)
{
    int i;
    for (i = 0; i < SUBPROCESS_HOOK_COUNT; i++) {
        if (p->hooks [i])
            zlist_destroy (&p->hooks [i]);
    }
}

struct subprocess * subprocess_create (struct subprocess_manager *sm)
{
    int fds[2];
    int saved_errno;
    struct subprocess *p = xzmalloc (sizeof (*p));

    memset (p, 0, sizeof (*p));
    p->childfd = -1;
    p->parentfd = -1;
    fda_zero (p->child_fda);

    p->sm = sm;
    if (!(p->zhash = zhash_new ())
     || hooks_table_init (p) < 0) {
        errno = ENOMEM;
        goto error;
    }

    p->pid = (pid_t) -1;
    p->refcount = 1;

    if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
        goto error;
    p->childfd = fds[0];
    p->parentfd = fds[1];

    p->started = 0;
    p->running = 0;
    p->exited = 0;
    p->completed = 0;

    if (!(p->zio_in = zio_pipe_writer_create ("stdin", (void *) p)))
        goto error;
    if (!(p->zio_out = zio_pipe_reader_create ("stdout", (void *) p)))
        goto error;
    if (!(p->zio_err = zio_pipe_reader_create ("stderr", (void *) p)))
        goto error;

    zio_set_send_cb (p->zio_out, output_handler);
    zio_set_send_cb (p->zio_err, output_handler);

    if (zlist_append (sm->processes, (void *)p) < 0) {
        errno = ENOMEM;
        goto error;
    }

    if (sm->reactor) {
        zio_reactor_attach (p->zio_in, sm->reactor);
        zio_reactor_attach (p->zio_err, sm->reactor);
        zio_reactor_attach (p->zio_out, sm->reactor);
    }
    return (p);
error:
    saved_errno = errno;
    subprocess_destroy (p);
    errno = saved_errno;
    return (NULL);
}

void subprocess_destroy (struct subprocess *p)
{
    subprocess_unref (p);
}

static void subprocess_free (struct subprocess *p)
{
    assert (p != NULL);

    if (p->sm)
        zlist_remove (p->sm->processes, (void *) p);
    zhash_destroy (&p->zhash);
    hooks_table_free (p);

    fda_closeall (p->child_fda);

    if (p->argz)
        free (p->argz);
    if (p->envz)
        free (p->envz);
    if (p->cwd)
        free (p->cwd);

    zio_destroy (p->zio_in);
    zio_destroy (p->zio_out);
    zio_destroy (p->zio_err);

    if (p->parentfd != -1)
        close (p->parentfd);
    if (p->childfd != -1)
        close (p->childfd);
    flux_watcher_destroy (p->child_watcher);

    memset (p, 0, sizeof (*p));
    free (p);
}

int
subprocess_flush_io (struct subprocess *p)
{
    zio_flush (p->zio_in);
    while (zio_read (p->zio_out) > 0) {};
    while (zio_read (p->zio_err) > 0) {};
    return (0);
}

int
subprocess_write (struct subprocess *p, void *buf, size_t n, bool eof)
{
    int rc = 0;
    if (n > 0)
        rc = zio_write (p->zio_in, buf, n);
    if (eof)
        zio_write_eof (p->zio_in);
    return (rc);
}

int subprocess_io_complete (struct subprocess *p)
{
    if (p->io_cb) {
        if (zio_closed (p->zio_out) && zio_closed (p->zio_err))
            return 1;
        return 0;
    }
    return 1;
}

int
subprocess_set_io_callback (struct subprocess *p, subprocess_io_cb_f fn)
{
    p->io_cb = fn;
    return (0);
}

int subprocess_add_hook (struct subprocess *p,
                         subprocess_hook_t t, subprocess_cb_f fn)
{
    zlist_append (p->hooks [t], fn);
    return (0);
}

int
subprocess_set_context (struct subprocess *p, const char *name, void *ctx)
{
    return zhash_insert (p->zhash, name, ctx);
}

void *
subprocess_get_context (struct subprocess *p, const char *name)
{
    return zhash_lookup (p->zhash, name);
}

static int init_argz (char **argzp, size_t *argz_lenp, char * const av[])
{
    int e;

    if (*argzp != NULL) {
        free (*argzp);
        *argz_lenp = 0;
    }

    if (av && (e = argz_create (av, argzp, argz_lenp)) != 0) {
        errno = e;
        return -1;
    }
    return (0);
}

int subprocess_set_args (struct subprocess *p, int argc, char **argv)
{
    if (p->started || (argv [argc] != NULL)) {
        errno = EINVAL;
        return (-1);
    }
    return (init_argz (&p->argz, &p->argz_len, argv));
}

int subprocess_set_args_from_argz (struct subprocess *p, const char * argz, size_t argz_len)
{
    if (p->started || argz == NULL) {
        errno = EINVAL;
        return (-1);
    }
    free (p->argz);
    p->argz_len = 0;
    int e = argz_append (&p->argz, &p->argz_len, argz, argz_len);
    return e ? (errno = e, -1) : 0;
}

const char * subprocess_get_arg (struct subprocess *p, int n)
{
    int i;
    char *entry = NULL;

    if (n > subprocess_get_argc (p))
        return (NULL);

    for (i = 0; i <= n; i++)
        entry = argz_next (p->argz, p->argz_len, entry);

    return (entry);
}

int subprocess_get_argc (struct subprocess *p)
{
    return (argz_count (p->argz, p->argz_len));
}

int subprocess_set_cwd (struct subprocess *p, const char *cwd)
{
    if (p->started) {
        errno = EINVAL;
        return (-1);
    }
    if (p->cwd)
        free (p->cwd);
    p->cwd = strdup (cwd);
    return (0);
}

const char *subprocess_get_cwd (struct subprocess *p)
{
    return (p->cwd);
}

int subprocess_set_environ (struct subprocess *p, char **env)
{
    return (init_argz (&p->envz, &p->envz_len, env));
}

int subprocess_argv_append (struct subprocess *p, const char *s)
{
    int e;

    if (p->started) {
        errno = EINVAL;
        return (-1);
    }

    if ((e = argz_add (&p->argz, &p->argz_len, s)) != 0) {
        errno = e;
        return -1;
    }
    return (0);
}

int subprocess_argv_append_argz (struct subprocess *p, const char *argz, size_t argz_len)
{
    int e;

    if (p->started) {
        errno = EINVAL;
        return (-1);
    }

    if ((e = argz_append (&p->argz, &p->argz_len, argz, argz_len)) != 0) {
        errno = e;
        return -1;
    }
    return (0);
}

int subprocess_set_command (struct subprocess *p, const char *cmd)
{
    int e;

    if (p->started) {
        errno = EINVAL;
        return (-1);
    }
    init_argz (&p->argz, &p->argz_len, NULL);

    if ((e = argz_add (&p->argz, &p->argz_len, "sh")) != 0
        || (e = argz_add (&p->argz, &p->argz_len, "-c")) != 0
        || (e = argz_add (&p->argz, &p->argz_len, cmd)) != 0) {
        errno = e;
        return (-1);
    }
    return (0);
}

int subprocess_setenv (struct subprocess *p,
    const char *k, const char *v, int overwrite)
{
    if (p->started) {
        errno = EINVAL;
        return (-1);
    }
    if (!overwrite && envz_entry (p->envz, p->envz_len, k)) {
        errno = EEXIST;
        return -1;
    }
    if (envz_add (&p->envz, &p->envz_len, k, v) < 0) {
        errno = ENOMEM;
        return (-1);
    }
    return (0);
}

int subprocess_setenvf (struct subprocess *p,
    const char *k, int overwrite, const char *fmt, ...)
{
    va_list ap;
    char *val;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return (rc);

    rc = subprocess_setenv (p, k, val, overwrite);
    free (val);
    return (rc);
}

int subprocess_unsetenv (struct subprocess *p, const char *name)
{
    if (p->started) {
        errno = EINVAL;
        return (-1);
    }
    envz_remove (&p->envz, &p->envz_len, name);
    return (0);
}

char * subprocess_getenv (struct subprocess *p, const char *name)
{
    return (envz_get (p->envz, p->envz_len, name));
}

static char **expand_argz (char *argz, size_t argz_len)
{
    size_t len;
    char **argv;

    len = argz_count (argz, argz_len) + 1;
    argv = xzmalloc (len * sizeof (char *));

    argz_extract (argz, argz_len, argv);

    return (argv);
}

char **subprocess_argv_expand (struct subprocess *p)
{
    return (expand_argz (p->argz, p->argz_len));
}

char **subprocess_env_expand (struct subprocess *p)
{
    envz_strip (&p->envz, &p->envz_len);
    return (expand_argz (p->envz, p->envz_len));
}

int subprocess_socketpair (struct subprocess *p, int *child_fd)
{
    int fds[2];

    if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
        return -1;
    if (fda_set (p->child_fda, fds[1]) < 0) {
        close (fds[0]);
        close (fds[1]);
        errno = ENFILE;
        return -1;
    }
    if (child_fd)
        *child_fd = fds[1];
    return fds[0];
}

void do_prepare_open_fd (void *arg, int fd)
{
    struct subprocess *p = arg;
    if (fd < 3 || fd == p->childfd)
        return;
    if (fda_isset (p->child_fda, fd)) {
        int flags = fcntl (fd, F_GETFD, 0);
        // XXX No way to return error to caller
        if (flags >= 0)
            (void) fcntl (fd, F_SETFD, flags & ~FD_CLOEXEC);
        return;
    }
    close (fd);
}

/* Close all fd's except the ones we are using.
 * Clear the close-on-exec flag for socketpair fd's we are passing to child.
 */
static int preparefd_child (struct subprocess *p)
{
    return fdwalk (do_prepare_open_fd, (void *) p);
}

static int dup2_fd (int fd, int newfd)
{
    assert (fd >= 0);
    assert (newfd >= 0);
    return dup2 (fd, newfd);
}

static int child_io_setup (struct subprocess *p)
{
    /*
     *  Close parent end of stdio in child:
     */
    if (zio_close_dst_fd (p->zio_in) < 0
            || zio_close_src_fd (p->zio_out) < 0
            || zio_close_src_fd (p->zio_err) < 0)
        return (-1);

    /*
     *  Dup this process' fds onto zio
     */
    if (  (dup2_fd (zio_src_fd (p->zio_in), STDIN_FILENO) < 0)
       || (dup2_fd (zio_dst_fd (p->zio_out), STDOUT_FILENO) < 0)
       || (dup2_fd (zio_dst_fd (p->zio_err), STDERR_FILENO) < 0))
        return (-1);

    return (0);
}

static int parent_io_setup (struct subprocess *p)
{
    /*
     *  Close child end of stdio in parent:
     */
    if (zio_close_src_fd (p->zio_in) < 0
            || zio_close_dst_fd (p->zio_out) < 0
            || zio_close_dst_fd (p->zio_err) < 0)
        return (-1);

    return (0);
}


static int sp_barrier_read_error (int fd)
{
    int e;
    ssize_t n = read (fd, &e, sizeof (int));
    if (n < 0) {
        log_err ("sp_read_error: read");
        return (-1);
    }
    else if (n == sizeof (int)) {
        /* exec failure */
        return (e);
    }
    return (0);
}

static int sp_barrier_signal (int fd)
{
    char c = 0;
    if (write (fd, &c, sizeof (c)) != 1) {
        log_err ("sp_barrier_signal: write");
        return (-1);
    }
    return (0);
}

static int sp_barrier_wait (int fd)
{
    char c;
    int n;
    if ((n = read (fd, &c, sizeof (c))) != 1) {
        log_err ("sp_barrier_wait: read:fd=%d: (got %d)", fd, n);
        return (-1);
    }
    return (0);
}

static void sp_barrier_write_error (int fd, int e)
{
    if (write (fd, &e, sizeof (int)) != sizeof (int)) {
        log_err ("sp_barrier_error: write");
    }
}

static void subprocess_child (struct subprocess *p)
{
    int errnum, code = 127;
    char **argv;

    sigmask_unblock_all ();
    close (p->parentfd);
    p->parentfd = -1;

    if (p->io_cb)
        child_io_setup (p);

    if (p->cwd && chdir (p->cwd) < 0) {
        log_err ("Couldn't change dir to %s: going to /tmp instead", p->cwd);
        if (chdir ("/tmp") < 0)
            _exit (1);
    }

    /*
     *  Send ready signal to parent
     */
    sp_barrier_signal (p->childfd);

    /*
     *  Wait for ready signal from parent
     */
    sp_barrier_wait (p->childfd);

    if (preparefd_child (p) < 0) {
        log_err ("Failed to prepare child fds");
        _exit (1);
    }

    if (subprocess_run_hooks (p, p->hooks [SUBPROCESS_PRE_EXEC]) < 0) {
        log_err ("Failed to run subprocess hooks: %s\n", strerror (errno));
        _exit (1);
    }

    environ = subprocess_env_expand (p);
    argv = subprocess_argv_expand (p);
    execvp (argv[0], argv);
    /*
     *  Exit code standards:
     *    126 for permission/access denied or
     *    127 for EEXIST (or anything else)
     */
    errnum = errno;
    if (errnum == EPERM || errnum == EACCES)
        code = 126;

    /*
     * XXX: close stdout and stderr here to avoid flushing buffers at exit.
     *  This can cause duplicate output if parent was running in fully
     *  bufferred mode, and there was buffered output.
     */
    close (STDOUT_FILENO);
    close (STDERR_FILENO);
    sp_barrier_write_error (p->childfd, errnum);
    _exit (code);
}

int subprocess_exec (struct subprocess *p)
{
    int rc = 0;
    if (sp_barrier_signal (p->parentfd) < 0)
        return (-1);

    if ((p->exec_error = sp_barrier_read_error (p->parentfd)) != 0) {
        /*
         * Reap child immediately. Expectation from caller is that
         *  a call to subprocess_reap will not be necessary after exec
         *  failure
         */
        subprocess_reap (p);
        rc = -1;
    }
    else {
        p->running = 1;
        subprocess_run_hooks (p, p->hooks [SUBPROCESS_RUNNING]);
    }

    /* No longer need parentfd socket */
    close (p->parentfd);
    p->parentfd = -1;
    if (rc < 0)
        errno = p->exec_error;
    return (rc);
}

static void subprocess_process_wait_status (struct subprocess *p, int status)
{
    if (status < 0)
        return;
    p->status = status;
    if (WIFEXITED (p->status) || WIFSIGNALED (p->status)) {
        p->exited = 1;
        subprocess_run_hooks (p, p->hooks [SUBPROCESS_EXIT]);
    }
    subprocess_run_hooks (p, p->hooks [SUBPROCESS_STATUS]);
    check_completion (p);
}


static void child_watcher (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    struct subprocess *p = arg;
    subprocess_process_wait_status (p, flux_child_watcher_get_rstatus (w));
}

int subprocess_fork (struct subprocess *p)
{
    if (p->argz_len <= 0 || p->argz == NULL || p->started) {
        errno = EINVAL;
        return -1;
    }

    if ((p->pid = fork ()) < 0)
        return (-1);

    if (p->pid == 0)
        subprocess_child (p); /* No return */

    if (p->io_cb)
        parent_io_setup (p);
    if (p->sm->reactor) {     /* no-op if reactor is !FLUX_REACTOR_SIGCHLD */
        p->child_watcher = flux_child_watcher_create (p->sm->reactor,
                                                      p->pid, true,
                                                      child_watcher, p);
        flux_watcher_start (p->child_watcher);
    }

    close (p->childfd);
    p->childfd = -1;

    fda_closeall (p->child_fda);

    sp_barrier_wait (p->parentfd);
    p->started = 1;

    return (subprocess_run_hooks (p, p->hooks [SUBPROCESS_POST_FORK]));
}

int subprocess_run (struct subprocess *p)
{
    if (subprocess_fork (p) < 0)
        return (-1);
    return subprocess_exec (p);
}

int subprocess_kill (struct subprocess *p, int sig)
{
    if (p->pid < (pid_t) 0)
        return (-1);
    return (kill (p->pid, sig));
}

pid_t subprocess_pid (struct subprocess *p)
{
    return (p->pid);
}

int subprocess_exit_status (struct subprocess *p)
{
    return (p->status);
}

int subprocess_exited (struct subprocess *p)
{
    return (p->exited);
}

int subprocess_exit_code (struct subprocess *p)
{
    int sig;
    int code = -1;
    if (WIFEXITED (p->status))
        code = WEXITSTATUS (p->status);
    else if ((sig = subprocess_signaled (p)))
        code = sig + 128;
    return (code);
}

int subprocess_signaled (struct subprocess *p)
{
    if (WIFSIGNALED (p->status))
        return (WTERMSIG (p->status));
    return (0);
}

int subprocess_stopped (struct subprocess *p)
{
    if (WIFSTOPPED (p->status))
        return (WSTOPSIG (p->status));
    return (0);
}

int subprocess_continued (struct subprocess *p)
{
    if (WIFCONTINUED (p->status))
        return (1);
    return (0);
}
int subprocess_exec_error (struct subprocess *p)
{
    return (p->exec_error);
}

const char * subprocess_state_string (struct subprocess *p)
{
    if (!p->started)
        return ("Pending");
    if (p->exec_error)
        return ("Exec Failure");
    if (!p->running)
        return ("Waiting");
    if (!p->exited)
        return ("Running");
    return ("Exited");
}

const char * subprocess_exit_string (struct subprocess *p)
{
    if (p->exec_error)
        return ("Exec Failure");

    if (!p->exited)
        return ("Process is still running or has not been started");

    if (WIFSIGNALED (p->status)) {
        int sig = WTERMSIG (p->status);
        return (strsignal (sig));
    }

    if (WEXITSTATUS (p->status) != 0)
        return ("Exited with non-zero status");

    return ("Exited");
}

struct subprocess_manager * subprocess_manager_create (void)
{
    struct subprocess_manager *sm = xzmalloc (sizeof (*sm));

    if (!(sm->processes = zlist_new ())) {
        errno = ENOMEM;
        free (sm);
        return (NULL);
    }

    return (sm);
}

void subprocess_manager_destroy (struct subprocess_manager *sm)
{
    size_t n = zlist_size (sm->processes);
    assert (n == 0);

    zlist_destroy (&sm->processes);
    free (sm);
}

static struct subprocess *
subprocess_manager_find_pid (struct subprocess_manager *sm, pid_t pid)
{
    struct subprocess *p = zlist_first (sm->processes);
    while (p) {
        if (p->pid == pid)
            return (p);
        p = zlist_next (sm->processes);
    }
    return (NULL);
}

struct subprocess *
subprocess_manager_first (struct subprocess_manager *sm)
{
    return zlist_first (sm->processes);
}

struct subprocess *
subprocess_manager_next (struct subprocess_manager *sm)
{
    return zlist_next (sm->processes);
}

struct subprocess *
subprocess_manager_run (struct subprocess_manager *sm, int ac, char **av,
    char **env)
{
    struct subprocess *p = subprocess_create (sm);
    if (p == NULL)
        return (NULL);

    if ((subprocess_set_args (p, ac, av) < 0) ||
        (env && subprocess_set_environ (p, env) < 0)) {
        subprocess_destroy (p);
        return (NULL);
    }

    if (subprocess_run (p) < 0) {
        subprocess_destroy (p);
        return (NULL);
    }

    return (p);
}

int subprocess_reap (struct subprocess *p)
{
    pid_t rc;
    int status;
    if (p->exited)
        return (0);
    rc = waitpid (p->pid, &status, 0);
    if (rc <= 0)
        return (-1);
    subprocess_process_wait_status (p, status);
    return (0);
}

struct subprocess *
subprocess_manager_wait (struct subprocess_manager *sm)
{
    int status;
    pid_t pid;
    struct subprocess *p;

    pid = waitpid (-1, &status, sm->wait_flags);
    if ((pid < 0) || !(p = subprocess_manager_find_pid (sm, pid))) {
        return (NULL);
    }
    subprocess_process_wait_status (p, status);
    return (p);
}

int
subprocess_manager_reap_all (struct subprocess_manager *sm)
{
    struct subprocess *p;
    while ((p = subprocess_manager_wait (sm)))
        check_completion (p);
    return (0);
}

int
subprocess_manager_set (struct subprocess_manager *sm, int item, ...)
{
    va_list ap;

    if (!sm)
        return (-1);

    va_start (ap, item);
    switch (item) {
        case SM_WAIT_FLAGS:
            sm->wait_flags = va_arg (ap, int);
            break;
        case SM_FLUX:
            sm->reactor = flux_get_reactor ((flux_t *) va_arg (ap, void *));
            break;
        case SM_REACTOR:
            sm->reactor = (flux_reactor_t *) va_arg (ap, void *);
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    va_end (ap);
    return (0);
}


/*
 *  vi: ts=4 sw=4 expandtab
 */
