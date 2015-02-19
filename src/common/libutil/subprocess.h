
struct subprocess_manager;
struct subprocess;

typedef enum sm_item {
    SM_WAIT_FLAGS
} sm_item_t;

typedef int (subprocess_cb_f) (struct subprocess *p, void *arg);

/*
 *  Create a subprocess manager to manage creation, destruction, and
 *   management of subprocess.
 */
struct subprocess_manager * subprocess_manager_create (void);

/*
 *  Set value for item [item] in subprocess manager [sm]
 */
int subprocess_manager_set (struct subprocess_manager *sm,
	sm_item_t item, ...);

/*
 *  Free memory associated with a subprocess manager object:
 */
void subprocess_manager_destroy (struct subprocess_manager *sm);

/*
 *  Execute a new subprocess with arguments argc,argv, and environment
 *   env. The child process will have forked, but not necessarily
 *   executed by the time this function returns.
 */
struct subprocess * subprocess_manager_run (struct subprocess_manager *sm,
	int argc, char *argv[], char **env);

/*
 *  Wait for any child to exit and return the handle of the exited
 *   subprocess to caller.
 */
struct subprocess * subprocess_manager_wait (struct subprocess_manager *sm);

int subprocess_manager_reap_all (struct subprocess_manager *sm);

/*
 *  Create a new, empty handle for a subprocess object.
 */
struct subprocess * subprocess_create (struct subprocess_manager *sm);

/*
 *  Set a callback function for subprocess exit
 */
int subprocess_set_callback (struct subprocess *p, subprocess_cb_f fn, void *arg);

/*
 *  Destroy a subprocess. Free memory and remove from subprocess
 *   manager list.
 */
void subprocess_destroy (struct subprocess *p);

/*
 *  Set an arbitrary context in the subprocess [p].
 */
void subprocess_set_context (struct subprocess *p, void *ctx);

/*
 *  Return the saved context for subprocess [p].
 */
void *subprocess_get_context (struct subprocess *p);

/*
 *  Set argument vector for subprocess [p]. This function is only valid
 *   before subprocess_run() is called. Any existing args associated with
 *   subprocess are discarded.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_args (struct subprocess *p, int argc, char *argv[]);

/*
 *  Identical subprocess_set_args(), subprocess_set_command() is a
 *   convenience function similar to system(3). That is, it will set
 *   arguments for subprocess [p] to
 *
 *     /bin/sh -c "command"
 *
 */
int subprocess_set_command (struct subprocess *p, const char *command);



/*
 *  Append a single argument to the subprocess [p] argument vector.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_argv_append (struct subprocess *p, const char *arg);

/*
 *  Get argument at index [n] from current argv array for process [p]
 *  Returns NULL if n > argc - 1.
 */
const char *subprocess_get_arg (struct subprocess *p, int n);

/*
 *  Return current argument count for subprocess [p].
 */
int subprocess_get_argc (struct subprocess *p);

/*
 *  Set working directory for subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_cwd (struct subprocess *p, const char *cwd);

/*
 *  Get working directory (if any) for subprocess [p].
 *   Returns (NULL) if no working directory is set.
 */
const char *subprocess_get_cwd (struct subprocess *p);

/*
 *  Set initial subprocess environment. This function is only valid
 *   before subprocess_run() is called. Any existing environment array
 *   associated with this subprocess is discarded.
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_set_environ (struct subprocess *p, char **env);

/*
 *  Setenv() equivalent with optional overwrite for subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_setenv (struct subprocess *p,
	const char *name, const char *val, int overwrite);

/*
 *   As above but allows formatted args.
 */
int subprocess_setenvf (struct subprocess *p,
	const char *name, int overwrite, const char *fmt, ...);

/*
 *  Unset [name] in the environment array of subprocess [p].
 *   Returns -1 with errno set to EINVAL if subprocess has already started.
 */
int subprocess_unsetenv (struct subprocess *p, const char *name);

/*
 *  getenv(3) equivalent for subprocess [p].
 */
char *subprocess_getenv (struct subprocess *p, const char *name);

/*
 *  Send signal [signo] to subprocess [p]
 */
int subprocess_kill (struct subprocess *p, int signo);

/*
 *  Return PID of process [p] if it is started.
 *   Returns (pid_t) -1 otherwise.
 */
pid_t subprocess_pid (struct subprocess *p);

/*
 *  Wait for and reap the subprocess [p]. After a successful return,
 *   subprocess_exited (p) will be true, and subprocess_exit* will
 *   be valid, etc.
 *  Returns -1 on failure.
 */
int subprocess_reap (struct subprocess *p);

/*
 *  Return 1 if subprocess [p] has exited.
 */
int subprocess_exited (struct subprocess *p);

/*
 *  Return exit status as returned by wait(2) for subprocess [p].
 */
int subprocess_exit_status (struct subprocess *p);

/*
 *  Return exit code for subprocess [p] if !subprocess_signaled()
 */
int subprocess_exit_code (struct subprocess *p);

/*
 *  Return number of the signal that caused process to exit,
 *   or 0 if process was not killed by a signal.
 */
int subprocess_signaled (struct subprocess *p);

/*
 *  Return string representation of process [p] current state,
 *   "Pending", "Exec Failure", "Waiting", "Running", "Exited"
 */
const char * subprocess_state_string (struct subprocess *p);

/*
 *  Convenience function returning a state string corresponding to
 *   process [p] exit status.
 */
const char * subprocess_exit_string (struct subprocess *p);

/*
 *  Fork, but wait to exec(), subprocess [p].
 *   Returns -1 and EINVAL if subprocess argv is not initialized.
 *           -1 and errno on fork() failure.
 */
int subprocess_fork (struct subprocess *p);

/*
 *   Unblock subprocess [p] and allow it to call exec.
 *    Returns -1 and EINVAL if subprocess is not in state 'started'.
 *            -1 and error if exec failed.
 */
int subprocess_exec (struct subprocess *p);

/*
 *  Same as calling subprocess_fork() + subprocess_exec()
 */
int subprocess_run (struct subprocess *p);

