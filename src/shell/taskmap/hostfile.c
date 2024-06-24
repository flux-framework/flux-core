/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell taskmap.hostfile plugin
 *
 * Read a list of hosts from a file, and assign tasks to hosts in order
 * they are listed.
 */
#define FLUX_SHELL_PLUGIN_NAME "taskmap.hostfile"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/taskmap.h>
#include <flux/hostlist.h>

#include "src/common/libutil/errprintf.h"

#include "builtins.h"


/*  Create a taskmap that represents 'ntasks' tasks mapped across a set
 *  of hosts in 'nodelist', ordered by hostlist 'hl'.
 */
char *taskmap_hostlist (int ntasks,
                        struct hostlist *nodelist,
                        struct hostlist *hl,
                        flux_error_t *errp)
{
    struct taskmap *map = NULL;
    char *result = NULL;
    const char *host = NULL;

    if (!(map = taskmap_create ()))
        goto error;

    /* Loop through hostlist hl until all tasks have been assigned to hosts
     */
    while (ntasks > 0) {
        int rank;
        if (host == NULL)
            host = hostlist_first (hl);
        if ((rank = hostlist_find (nodelist, host)) < 0) {
            errprintf (errp, "host %s not found in job nodelist", host);
            goto error;
        }
        if (taskmap_append (map, rank, 1, 1) < 0) {
            errprintf (errp,
                       "failed to append task to taskmap: %s",
                       strerror (errno));
            goto error;
        }
        host = hostlist_next (hl);
        ntasks--;
    }
    result = taskmap_encode (map, TASKMAP_ENCODE_WRAPPED);
error:
    taskmap_destroy (map);
    return result;
}

static struct hostlist *hostlist_from_file (const char *path)
{
    ssize_t n;
    size_t size;
    struct hostlist *hl = NULL;
    FILE *fp = NULL;
    char *line = NULL;

    if (!(fp = fopen (path, "r"))) {
        shell_log_errno ("failed to open hostfile: %s", path);
        goto error;
    }
    if (!(hl = hostlist_create ())) {
        shell_log_errno ("failed to create hostlist");
        goto error;
    }
    while ((n = getline (&line, &size, fp)) != -1) {
        int len = strlen (line);
        if (line[len-1] == '\n')
            line[len-1] = '\0';
        if (strlen (line) > 0 && hostlist_append (hl, line) < 0) {
            shell_log_errno ("hostlist_append: %s", line);
        }
    }
error:
    if (fp)
        fclose (fp);
    free (line);
    return hl;
}

static int map_hostfile (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    flux_shell_t *shell;
    int rc = -1;
    const char *value = NULL;
    struct hostlist *hl = NULL;
    struct hostlist *nodelist = NULL;
    char *map = NULL;
    int ntasks;
    flux_error_t error;

    if (!(shell = flux_plugin_get_shell (p)))
        return -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s?s}",
                                "value", &value) < 0) {
        shell_log_error ("unpack: %s", flux_plugin_arg_strerror (args));
        return -1;
    }

    /*  Note: the result of flux_shell_get_hostlist(3) is copied because
     *  functions such as hostlist_find(3) modify the hostlist cursor, so
     *  a `const struct hostlist` can't be used here.
     */
    if (!(hl = hostlist_from_file (value))
        || !(nodelist = hostlist_copy (flux_shell_get_hostlist (shell)))) {
        shell_log_error ("failed to get hostlists from file and R");
        goto out;
    }
    if ((ntasks = taskmap_total_ntasks (flux_shell_get_taskmap (shell))) < 0)
        shell_log_error ("failed to get ntasks from current shell taskmap");

    if (!(map = taskmap_hostlist (ntasks, nodelist, hl, &error))) {
        shell_log_error ("failed to map tasks with hostfile:%s: %s",
                         value,
                         error.text);
        goto out;
    }
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:s}",
                              "taskmap", map) < 0) {
        shell_log_error ("failed to set new taskmap in plugin output args");
        goto out;
    }
    rc = 0;
out:
    free (map);
    hostlist_destroy (hl);
    hostlist_destroy (nodelist);
    return rc;
}

static int plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "taskmap.hostfile", map_hostfile, NULL);
}

struct shell_builtin builtin_hostfile = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .plugin_init = plugin_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
