#define FLUX_SHELL_PLUGIN_NAME "env-plugin"
#include <flux/shell.h>

static int task_init_cb (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    flux_cmd_t *cmd = flux_shell_task_cmd (task);

    int rank, localid;
    if (flux_shell_task_info_unpack (task,
                                     "{s:i s:i}",
                                     "rank", &rank,
                                     "localid", &localid) < 0)
        return shell_log_errno ("task_info_unpack");

    // Set custom environment variables
    if (flux_cmd_setenvf (cmd, 1, "MY_TASK_RANK", "%d", rank) < 0 ||
        flux_cmd_setenvf (cmd, 1, "MY_LOCAL_RANK", "%d", localid) < 0 ||
        flux_cmd_setenvf (cmd, 1, "MY_PLUGIN_ENABLED", "1") < 0) {
        shell_log_errno ("flux_cmd_setenvf");
        return -1;
    }

    shell_debug ("Set environment for task %d (local %d)", rank, localid);

    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "task.init",
                                    task_init_cb,
                                    NULL);
}

