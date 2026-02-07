#define FLUX_SHELL_PLUGIN_NAME "exit-monitor"
#include <flux/shell.h>
#include <sys/wait.h>

static int task_exit_cb (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    int rank, status;

    if (flux_shell_task_info_unpack (task,
                                     "{s:i s:i}",
                                     "rank", &rank,
                                     "wait_status", &status) < 0) {
        shell_log_errno ("task_info_unpack");
        return -1;
    }

    if (WIFEXITED (status)) {
        int exitcode = WEXITSTATUS (status);
        if (exitcode != 0) {
            shell_log ("task %d exited with code %d", rank, exitcode);

            // Optionally raise exception for critical failures
            if (exitcode == 42)
                 flux_shell_raise ("exit-monitor",
                                    1,
                                    "critical error task %d",
                                    rank);
        }
    }
    else if (WIFSIGNALED (status)) {
        int signum = WTERMSIG (status);
        shell_log_error ("task %d terminated by signal %d", rank, signum);
    }

    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "task.exit",
                                    task_exit_cb,
                                    NULL);
}

