 #define FLUX_SHELL_PLUGIN_NAME "config-test"
#include <flux/shell.h>

struct config {
    int enabled;
    const char *mode;
    int level;
};

static int read_config (flux_shell_t *shell, struct config *cfg)
{
    // Set defaults
    cfg->enabled = 1;
    cfg->mode = "default";
    cfg->level = 1;

    // Read options (failures leave defaults in place)
    flux_shell_getopt_unpack (shell,
                              "config-test",
                              "{s?b s?s s?i}",
                              "enabled", &cfg->enabled,
                              "mode", &cfg->mode,
                              "level", &cfg->level);

    return 0;
}

static int shell_init_cb (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct config *cfg;

    cfg = calloc (1, sizeof (*cfg));

    if (read_config (shell, cfg) < 0)
        return 0;
    if (!cfg->enabled) {
        shell_debug ("disabled by config-test.enabled=false");
        return 0;
    }

    shell_log ("initialized with mode=%s, level=%d", cfg->mode, cfg->level);

    /* Stash config in plugin aux item hash for later retrieval via
     * flux_plugin_aux_get(3) and automatic cleanup via free(3) when
     * plugin is destroyed:
     */
    flux_plugin_aux_set (p, "config", cfg, free);

    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p,
                                    "shell.init",
                                    shell_init_cb,
                                    NULL);
}
