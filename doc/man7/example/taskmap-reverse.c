#define FLUX_SHELL_PLUGIN_NAME "taskmap.reverse"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/taskmap.h>

static char *taskmap_reverse (const char *arg)
{
    struct taskmap *map = NULL;
    struct taskmap *orig = NULL;
    char *result = NULL;
    int nnodes;
    if (!(orig = taskmap_decode (arg, NULL))
        || !(map = taskmap_create ()))
        goto error;

    nnodes = taskmap_nnodes (orig);
    for (int i = nnodes - 1; i >= 0; i--) {
        if (taskmap_append (map, i, 1, taskmap_ntasks (orig, i)) < 0)
            goto error;
    }
    result = taskmap_encode (map, TASKMAP_ENCODE_WRAPPED);
error:
    taskmap_destroy (orig);
    taskmap_destroy (map);
    return result;
}

static int map_reverse (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    const char *blockmap;
    char *map;
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "taskmap", &blockmap) < 0) {
        shell_log_error ("unpack: %s", flux_plugin_arg_strerror (args));
        return -1;
    }
    if (!(map = taskmap_reverse (blockmap))) {
        shell_log_error ("failed to map tasks reverse");
        return -1;
    }
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:s}",
                              "taskmap", map) < 0)
        return -1;
    free (map);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "taskmap.reverse", map_reverse, NULL);
}
