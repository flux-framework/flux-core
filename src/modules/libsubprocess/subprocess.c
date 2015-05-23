#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/socket.h> /* socketpair(2) */
#include <sys/wait.h>
#include <stdio.h>
#include <stdarg.h>
#include <czmq.h>
#include <argz.h>
#include <envz.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/modules/libzio/zio.h"
#include "subprocess.h"

struct subprocess_manager {
    zlist_t *processes;
    int wait_flags;
};

struct subprocess {
    struct subprocess_manager *sm;
    void *ctx;

    pid_t pid;

    /* socketpair for synchronization */
    int parentfd;
    int childfd;

    char *cwd;

    size_t argz_len;
    char *argz;

    size_t envz_len;
    char *envz;

    int status;
    int exec_error;

    unsigned short started:1;
    unsigned short execed:1;
    unsigned short running:1;
    unsigned short exited:1;

    zio_t zio_in;
    zio_t zio_out;
    zio_t zio_err;

    subprocess_cb_f *exit_cb;
    void *exit_cb_arg;

    subprocess_io_cb_f *io_cb;
};

static int sigmask_unblock_all (void)
{
    sigset_t mask;
    sigemptyset (&mask);
    return sigprocmask (SIG_SETMASK, &mask, NULL);
}

/*
 *  Default handler for stdout/err: send output directly into
 *   stderr of caller...
 */
static int send_output_to_stream (const char *name, json_object *o)
{
    FILE *fp = stdout;
    char *s;
    bool eof;

    int len = zio_json_decode (o, (void **) &s, &eof);

    if (strcmp (name, "stderr") == 0)
        fp = stderr;

    if (len < 0)
        return (-1);
    if (len > 0)
        fputs (s, fp);
    if (eof)
        fclose (fp);

    return (len);
}

static int output_handler (zio_t z, json_object *o, void *arg)
{
    struct subprocess *p = (struct subprocess *) arg;

    if (p->io_cb)
        return (*p->io_cb) (p, o);

    return send_output_to_stream (zio_name (z), o);
}

struct subprocess * subprocess_create (struct subprocess_manager *sm)
{
    int fds[2];
    struct subprocess *p = xzmalloc (sizeof (*p));

    p->sm = sm;

    p->pid = (pid_t) -1;

    if (socketpair (PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
        msg ("socketpair: %m");
        free (p);
        return (NULL);
    }
    p->childfd = fds[0];
    p->parentfd = fds[1];

    p->started = 0;
    p->running = 0;
    p->exited = 0;

    p->zio_in = zio_pipe_writer_create ("stdin", (void *) p);
    p->zio_out = zio_pipe_reader_create ("stdout", NULL, (void *) p);
    p->zio_err = zio_pipe_reader_create ("stderr", NULL, (void *) p);

    zio_set_send_cb (p->zio_out, (zio_send_f) output_handler);
    zio_set_send_cb (p->zio_err, (zio_send_f) output_handler);

    zlist_append (sm->processes, (void *)p);

    return (p);
}

void subprocess_destroy (struct subprocess *p)
{
    if (p->sm)
        zlist_remove (p->sm->processes, (void *) p);

    p->sm = NULL;
    free (p->argz);
    p->argz = NULL;
    p->argz_len = 0;
    free (p->envz);
    p->envz = NULL;
    p->envz_len = 0;

    free (p->cwd);

    zio_destroy (p->zio_in);
    zio_destroy (p->zio_out);
    zio_destroy (p->zio_err);

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
    if (eof)
        zio_write_eof (p->zio_in);
    return zio_write (p->zio_in, buf, n);
}

int
subprocess_set_callback (struct subprocess *p, subprocess_cb_f fn, void *arg)
{
    p->exit_cb = fn;
    p->exit_cb_arg = arg;
    return (0);
}

int
subprocess_set_io_callback (struct subprocess *p, subprocess_io_cb_f fn)
{
    p->io_cb = fn;
    return (0);
}

void
subprocess_set_context (struct subprocess *p, void *ctx)
{
    p->ctx = ctx;
}

void *
subprocess_get_context (struct subprocess *p)
{
    return (p->ctx);
}

static int init_argz (char **argzp, size_t *argz_lenp, char * const av[])
{
    if (*argzp != NULL) {
        free (*argzp);
        *argz_lenp = 0;
    }

    if (av && argz_create (av, argzp, argz_lenp) < 0) {
        errno = ENOMEM;
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
    if (p->started) {
        errno = EINVAL;
        return (-1);
    }

    if (argz_add (&p->argz, &p->argz_len, s) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return (0);
}

int subprocess_set_command (struct subprocess *p, const char *cmd)
{
    if (p->started) {
        errno = EINVAL;
        return (-1);
    }
    init_argz (&p->argz, &p->argz_len, NULL);

    if (argz_add (&p->argz, &p->argz_len, "sh") < 0
        || argz_add (&p->argz, &p->argz_len, "-c") < 0
        || argz_add (&p->argz, &p->argz_len, cmd) < 0) {
        errno = ENOMEM;
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

static void closeall (int fd, int except)
{
    int fdlimit = sysconf (_SC_OPEN_MAX);

    while (fd < fdlimit) {
        if (fd != except)
            close (fd);
        fd++;
    }
    return;
}

static int child_io_setup (struct subprocess *p)
{
    /*
     *  Close paretn end of stdio in child:
     */
    close (zio_dst_fd (p->zio_in));
    close (zio_src_fd (p->zio_out));
    close (zio_src_fd (p->zio_err));

    /*
     *  Dup this process' fds onto zio
     */
    if (  (dup2 (zio_src_fd (p->zio_in), STDIN_FILENO) < 0)
       || (dup2 (zio_dst_fd (p->zio_out), STDOUT_FILENO) < 0)
       || (dup2 (zio_dst_fd (p->zio_err), STDERR_FILENO) < 0))
        return (-1);

    return (0);
}

static int parent_io_setup (struct subprocess *p)
{
    /*
     *  Close child end of stdio in parent:
     */
    close (zio_src_fd (p->zio_in));
    close (zio_dst_fd (p->zio_out));
    close (zio_dst_fd (p->zio_err));

    return (0);
}


static int sp_barrier_read_error (int fd)
{
    int e;
    ssize_t n = read (fd, &e, sizeof (int));
    if (n < 0) {
        err ("sp_read_error: read: %m");
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
        err ("sp_barrier_signal: write: %m");
        return (-1);
    }
    return (0);
}

static int sp_barrier_wait (int fd)
{
    char c;
    int n;
    if ((n = read (fd, &c, sizeof (c))) != 1) {
        err ("sp_barrier_wait: read:fd=%d: (got %d): %m", fd, n);
        return (-1);
    }
    return (0);
}

static void sp_barrier_write_error (int fd, int e)
{
    if (write (fd, &e, sizeof (int)) != sizeof (int)) {
        err ("sp_barrier_error: write: %m");
    }
}

static void subprocess_child (struct subprocess *p)
{
    char **argv;

    sigmask_unblock_all ();
    close (p->parentfd);
    p->parentfd = -1;

    if (p->io_cb)
        child_io_setup (p);

    if (p->cwd && chdir (p->cwd) < 0) {
        err ("Couldn't change dir to %s: going to /tmp instead", p->cwd);
        if (chdir ("/tmp") < 0)
            exit (1);
    }

    /*
     *  Send ready signal to parent
     */
    sp_barrier_signal (p->childfd);

    /*
     *  Wait for ready signal from parent
     */
    sp_barrier_wait (p->childfd);

    closeall (3, p->childfd);

    environ = subprocess_env_expand (p);
    argv = subprocess_argv_expand (p);
    execvp (argv[0], argv);
    /*
     * XXX: close stdout and stderr here to avoid flushing buffers at exit.
     *  This can cause duplicate output if parent was running in fully
     *  bufferred mode, and there was buffered output.
     */
    close (STDOUT_FILENO);
    close (STDERR_FILENO);
    sp_barrier_write_error (p->childfd, errno);
    exit (127);
}

int subprocess_exec (struct subprocess *p)
{
    if (sp_barrier_signal (p->parentfd) < 0)
        return (-1);

    if ((p->exec_error = sp_barrier_read_error (p->parentfd)) != 0) {
        /*  reap child */
        subprocess_reap (p);
        errno = p->exec_error;
        return (-1);
    }

    p->running = 1;

    /* No longer need parentfd socket */
    close (p->parentfd);
    p->parentfd = -1;
    return (0);
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
    close (p->childfd);
    p->childfd = -1;

    sp_barrier_wait (p->parentfd);
    p->started = 1;
    return (0);
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
    if (WIFEXITED (p->status))
        return (WEXITSTATUS (p->status));
    return (-1);
}

int subprocess_signaled (struct subprocess *p)
{
    if (WIFSIGNALED (p->status))
        return (WTERMSIG (p->status));
    return (0);
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

    sm->processes = zlist_new ();

    return (sm);
}

void subprocess_manager_destroy (struct subprocess_manager *sm)
{
    size_t n = zlist_size (sm->processes);
    assert (n == 0);

    zlist_destroy (&sm->processes);
    free (sm);
}

struct subprocess *
subprocess_manager_find (struct subprocess_manager *sm, pid_t pid)
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
    if (waitpid (p->pid, &p->status, p->sm->wait_flags) == (pid_t) -1)
        return (-1);
    p->exited = 1;
    return (0);
}

struct subprocess *
subprocess_manager_wait (struct subprocess_manager *sm)
{
    int status;
    pid_t pid;
    struct subprocess *p;

    pid = waitpid (-1, &status, sm->wait_flags);
    if ((pid < 0) || !(p = subprocess_manager_find (sm, pid))) {
        return (NULL);
    }
    p->status = status;
    p->exited = 1;
    return (p);
}

int
subprocess_manager_reap_all (struct subprocess_manager *sm)
{
    struct subprocess *p;
    while ((p = subprocess_manager_wait (sm))) {
        if (p->exit_cb) {
            if ((*p->exit_cb) (p, p->exit_cb_arg) < 0)
                return (-1);
            subprocess_destroy (p);
        }
    }
    return (0);
}

int
subprocess_manager_set (struct subprocess_manager *sm, sm_item_t item, ...)
{
    va_list ap;

    if (!sm)
        return (-1);

    va_start (ap, item);
    switch (item) {
        case SM_WAIT_FLAGS:
            sm->wait_flags = va_arg (ap, int);
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
