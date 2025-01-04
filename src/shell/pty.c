/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define FLUX_SHELL_PLUGIN_NAME "pty"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <jansson.h>

#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "src/common/libterminus/pty.h"
#include "src/common/libterminus/terminus.h"
#include "ccan/ptrint/ptrint.h"
#include "ccan/str/str.h"
#include "builtins.h"
#include "log.h"

static struct flux_terminus_server *
shell_terminus_server_start (flux_shell_t *shell, const char *shell_service)
{
    char service[128];
    struct flux_terminus_server *t;

    if (snprintf (service,
                  sizeof (service),
                  "%s.terminus",
                  shell_service) >= sizeof (service)) {
        shell_log_errno ("Failed to build terminus service name");
        return NULL;
    }

    /*  Create a terminus server in this shell. 1 per shell */
    t = flux_terminus_server_create (flux_shell_get_flux (shell),
                                     service);
    if (!t) {
        shell_log_errno ("flux_terminus_server_create");
        return NULL;
    }
    if (flux_shell_aux_set (shell,
                            "builtin::terminus",
                            t,
                            (flux_free_f) flux_terminus_server_destroy) < 0)
        return NULL;
    flux_terminus_server_set_log (t, shell_llog, NULL);

    /* Ensure process knows it is a terminus session */
    flux_shell_setenvf (shell, 1, "FLUX_TERMINUS_SESSION", "0");

    return t;
}

static void pty_monitor (struct flux_pty *pty, void *data, int len)
{
    flux_plugin_arg_t *args;
    const char *rank;

    /*  len == 0 indicates pty is closed. If there's a reference on
     *  stdout, release it here
     */
    if (len == 0) {
        flux_subprocess_t *p;
        if ((p = flux_pty_aux_get (pty, "subprocess")))
            flux_subprocess_channel_decref (p, "stdout");
        return;
    }

    rank = flux_pty_aux_get (pty, "rank");
    if (!(args = flux_plugin_arg_create ())
        || flux_plugin_arg_pack (args,
                                 FLUX_PLUGIN_ARG_IN,
                                 "{s:s s:s s:s#}",
                                 "stream", "stdout",
                                 "rank", rank,
                                 "data", data, len) < 0) {
        shell_log_errno ("monitor: packing %d bytes of shell.output: %s",
                         len,
                         flux_plugin_arg_strerror (args));
        return;
    }
    flux_shell_plugstack_call (flux_pty_aux_get (pty, "shell"),
                               "shell.output", args);
    flux_plugin_arg_destroy (args);
}

/*  Return an idset of ids that intersect the local taskids on shell rank
 *   given the idset encoded in `ids` ("all" will intersect with all ids).
 */
static struct idset *shell_taskids_intersect (flux_shell_t *shell,
                                              int rank,
                                              const char *ids)
{
    const char *taskids;
    struct idset *localids;
    struct idset *idset;
    struct idset *result = NULL;

    if (flux_shell_rank_info_unpack (shell,
                                     rank,
                                     "{s:s}",
                                     "taskids", &taskids) < 0)
        return NULL;
    if (!(localids = idset_decode (taskids)))
        return NULL;
    if (streq (ids, "all"))
        return localids;
    if (!(idset = idset_decode (ids)))
        goto out;
    result = idset_intersect (localids, idset);
out:
    idset_destroy (localids);
    idset_destroy (idset);
    return result;
}


/*  Parse any shell 'pty' option.
 *
 *  The shell pty option has the form:
 *
 *  {
 *     rasks:s or i   # rank or rank on which to open a pty
 *     capture:i      # if nonzero, capture pty output to the same
 *                    #  destination as task output
 *     interactive:i  # if nonzero, note pty endpoint in shell.init
 *                    #  for interactive attach from client
 *  }
 *
 *  The default if none of the above are set is pty.ranks = "all".
 *  If pty.interactive is nonzero, the default is pty.ranks = "0".
 *
 *  Return 0 if the option was not present,
 *         1 if the option was present and parsed without error,
 *    and -1 if the option was present and had a parse error.
 */
static int pty_getopt (flux_shell_t *shell,
                       int shell_rank,
                       struct idset **targets,
                       int *capture,
                       int *interactive)
{
    char *s;
    const char *ranks;
    char rbuf [21];
    json_t *o;

    /*  Only create a session for rank 0 if the pty option was specified
     */
    if (flux_shell_getopt (shell, "pty", &s) != 1)
        return 0;

    /*  Default: pty on all ranks with "non-interactive" attach
     *   and pty output is copied to stdout location.
     */
    ranks = "all";
    *interactive = 0;
    *capture = -1;

    if (!(o = json_loads (s, JSON_DECODE_ANY, NULL))) {
        shell_log_error ("Unable to parse pty shell option: %s", s);
        return -1;
    }
    if (json_is_object (o)) {
        json_error_t error;
        json_t *ranks_obj = NULL;

        if (json_unpack_ex (o,
                            &error,
                            JSON_STRICT,
                            "{s?o s?i s?i}",
                            "ranks", &ranks_obj,
                            "capture", capture,
                            "interactive", interactive) < 0) {
            shell_die (1, "invalid shell pty option: %s", error.text);
            return -1;
        }

        if (*interactive) {
            /*  If pty.interactive is set and pty.ranks is not, then
             *   default pty.ranks to "0"
             */
            if (ranks_obj == NULL)
                ranks = "0";

            /*  If pty.interactive is set and capture was not set
             *   then disable capture.
             */
            if (*capture == -1)
                *capture = 0;
        }

        /*  Allow ranks to be encoded as a string (for RFC 22 IDSet)
         *   or as an integer for a single rank (e.g. 0).
         */
        if (json_is_string (ranks_obj))
            ranks = json_string_value (ranks_obj);
        else if (json_is_integer (ranks_obj)) {
            /*  32bit unsigned guaranteed to fit in 21 bytes */
            sprintf (rbuf, "%u", (uint32_t) json_integer_value (ranks_obj));
            ranks = rbuf;
        }

        /*  Default for capture if not set is 1/true
         */
        if (*capture == -1)
            *capture = 1;
    }
    if (!(*targets = shell_taskids_intersect (shell, shell_rank, ranks))) {
        shell_log_error ("pty: shell_taskids_intersect");
        return -1;
    }

    /*  If interactive, then always ensure rank 0 is in the set of targets
     *  (interactive attach to non-rank 0 task is not yet supported)
     */
    if (*interactive
        && shell_rank == 0
        && !idset_test (*targets, 0)) {
        shell_warn ("pty: adding pty to rank 0 for interactive support");
        idset_set (*targets, 0);
    }
    return 1;
}

static void server_empty (struct flux_terminus_server *ts, void *arg)
{
    flux_shell_t *shell = arg;
    if (flux_shell_remove_completion_ref (shell, "terminus.server") < 0)
        shell_log_errno ("failed to remove completion ref for terminus.server");
}


static int pty_init (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    const char *shell_service;
    int shell_rank = -1;
    flux_shell_t *shell;
    struct flux_terminus_server *t;
    int interactive = 0;
    struct idset *targets;
    int rank;
    int capture;
    int rc;

    if (!(shell = flux_plugin_get_shell (p)))
        return shell_log_errno ("flux_plugin_get_shell");

    if (flux_shell_info_unpack (shell,
                                "{s:i s:s}",
                                "rank", &shell_rank,
                                "service", &shell_service) < 0)
        return shell_log_errno ("flux_shell_info_unpack: service");

    /*  Start terminus server for all shells
     */
    if (!(t = shell_terminus_server_start (shell, shell_service))) {
        shell_log_errno ("pty_init: error setting up terminal server");
        return -1;
    }

    if ((rc = pty_getopt (shell,
                          shell_rank,
                          &targets,
                          &capture,
                          &interactive)) != 1)
        return rc;

    if (idset_count (targets) > 0) {
        /*
         *   If there is at least one pty active on this shell rank,
         *    ensure shell doesn't exit until the terminus server is complete,
         *    even if all tasks have exited. This is required to support
         *    an interactive attach from a pty client, which may come after
         *    the task has exited.
         */
        if (flux_shell_add_completion_ref (shell, "terminus.server") < 0
            || flux_terminus_server_notify_empty (t,
                                                  server_empty,
                                                  shell) < 0) {
            shell_log_errno ("failed to enable pty server notification");
            return -1;
        }
    }


    /*  Create a pty session for each local target
     */
    rank = idset_first (targets);
    while (rank != IDSET_INVALID_ID) {
        struct flux_pty *pty;
        char name [26];
        char key [35];

        /*  task<int> guaranteed to fit in 25 characters */
        sprintf (name, "task%d", rank);
        /*  builtin::pty.<int> guaranteed to fit in 34 characters */
        sprintf (key, "builtin::pty.%d", rank);

        /*  Open a new terminal session for this rank
         */
        if (!(pty = flux_terminus_server_session_open (t, rank, name)))
            return shell_log_errno ("terminus_session_open");

        if (flux_shell_aux_set (shell, key, pty, NULL) < 0)
            goto error;

        /*  Always wait for the pty to be "closed" so that we ensure
         *   all data is read before the pty exits
         */
        flux_pty_wait_on_close (pty);

        /*  For an interactive pty, add the endpoint in the shell.init
         *   event context. This lets `flux job attach` or other entities
         *   know that the pty is ready for attach, and also lets them
         *   key off the presence of this value to know that an interactive
         *   pty was requested.
         */
        if (interactive && rank == 0) {
            if (flux_shell_add_event_context (shell,
                                              "shell.init",
                                              0,
                                              "{s:s}",
                                              "pty", "terminus.0") < 0) {
                shell_log_errno ("flux_shell_add_event_context (pty)");
                goto error;
            }
            if (capture) {
                /*
                 * If also capturing the pty output for an interactive
                 *  pty, note this in the shell.init event context. This
                 *  will hint to the pty reader that the terminal output
                 *  is duplicated for rank 0.
                 */
                if (flux_shell_add_event_context (shell,
                                                  "shell.init",
                                                  0,
                                                  "{s:i}",
                                                  "capture", 1) < 0) {
                    shell_log_errno ("flux_shell_add_event_context (capture)");
                }
            }
            /*  Ensure that rank 0 pty waits for client to attach
             *   in pty.interactive mode, even if pty.capture is also
             *   specified.
             */
            flux_pty_wait_for_client (pty);
        }

        /*  Enable capture of pty output to stdout if capture flag is set.
         *
         *  Always enable capture on nonzero ranks though, otherwise
         *   reading from the pty will never be started since nonozero
         *   ranks do not support interactive attach.
         */
        if (capture || rank != 0) {
            char *rankstr = NULL;

            if (asprintf (&rankstr, "%d", rank) < 0
                || flux_pty_aux_set (pty, "shell", shell, NULL) < 0
                || flux_pty_aux_set (pty, "rank", rankstr, free) < 0
                || flux_pty_aux_set (pty,
                                     "capture",
                                     int2ptr (capture),
                                     NULL) < 0) {
                free (rankstr);
                shell_log_errno ("flux_pty_aux_set");
                goto error;
            }
            flux_pty_monitor (pty, pty_monitor);
        }
        rank = idset_next (targets, rank);
    }
    idset_destroy (targets);
    return 0;
error:
    idset_destroy (targets);
    flux_terminus_server_destroy (t);
    return -1;
}

static struct flux_pty *pty_lookup (flux_shell_t *shell, int rank)
{
    char key [35];
    sprintf (key, "builtin::pty.%d", rank);
    return flux_shell_aux_get (shell, key);
}

static int pty_task_exec (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    struct flux_pty *pty;
    int rank;

    if (!shell)
        return shell_log_errno ("failed to get shell object");

    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    if (!(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return shell_log_errno ("unable to get task rank");

    if ((pty = pty_lookup (shell, rank))) {
        /*  Redirect stdio to 'pty'
         */
        if (pty && flux_pty_attach (pty) < 0)
            return shell_log_errno ("pty attach failed");

        /*  Set environment variable so process knows it is running
         *   under a terminus server.
         */
        flux_shell_setenvf (shell, 1, "FLUX_TERMINUS_SESSION", "%d", rank);
    }
    return (0);
}

static int pty_task_fork (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    struct flux_pty *pty;
    int rank;

    if (!shell)
        return shell_log_errno ("failed to get shell object");

    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    if (!(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return shell_log_errno ("unable to get task rank");

    /*  If pty is in capture mode, then take a reference on subprocess
     *  stdout so that EOF is not read until pty exits.
     */
   if ((pty = pty_lookup (shell, rank))
       && ptr2int (flux_pty_aux_get (pty, "capture"))) {
       flux_subprocess_t *sp = flux_shell_task_subprocess (task);
       flux_subprocess_channel_incref (sp, "stdout");
       flux_pty_aux_set (pty, "subprocess", sp, NULL);
   }
   return (0);
}

static int pty_task_exit (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    struct flux_pty *pty;
    int rank;

    if (!shell)
        return shell_log_errno ("failed to get shell object");

    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    if (!(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return shell_log_errno ("unable to get task rank");

    if ((pty = pty_lookup (shell, rank))) {
        struct flux_terminus_server *t = NULL;
        int status = flux_subprocess_status (flux_shell_task_subprocess (task));

        if (!(t = flux_shell_aux_get (shell, "builtin::terminus")))
            return shell_log_errno ("failed to get terminus and pty objects");

        shell_debug ("close pty session rank=%d status=%d", rank, status);
        if (flux_terminus_server_session_close (t, pty, status) < 0)
            shell_die_errno (1, "pty attach failed");
    }
    return (0);
}

struct shell_builtin builtin_pty = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = pty_init,
    .task_exec = pty_task_exec,
    .task_fork = pty_task_fork,
    .task_exit = pty_task_exit,
};

/* vi: ts=4 sw=4 expandtab
 */
