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

#include "builtin.h"

#include <sys/types.h> /* WIFEXTED */
#include <sys/wait.h>
#include <sys/param.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <argz.h>
#include <inttypes.h>

#include <hwloc.h>

#include "src/common/libutil/sds.h"

struct hwloc_topo {
    flux_t *h;
    flux_rpc_t *rpc;
    const char *topo;
};

/*
 * Gather concatenated hwloc xml topo file with resource-hwloc.topo RPC
 *  and save results until destroyed by hwloc_topo_destroy ().
 */
static struct hwloc_topo * hwloc_topo_create (optparse_t *p)
{
    struct hwloc_topo *t = xzmalloc (sizeof (*t));

    if (!(t->h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");

    if (!(t->rpc = flux_rpc (t->h, "resource-hwloc.topo", NULL, 0, 0)))
        log_err_exit ("flux_rpc");

    if (flux_rpc_getf (t->rpc, "{ s:s }", "topology", &t->topo) < 0)
        log_err_exit ("flux_rpc_getf");

    return (t);
}

/*
 * Free RPC for hwloc toplogy
 */
static void hwloc_topo_destroy (struct hwloc_topo *t)
{
    flux_rpc_destroy (t->rpc);
    flux_close (t->h);
    free (t);
}

const char *hwloc_topo_topology (struct hwloc_topo *t)
{
    return (t->topo);
}

static void lstopo_argz_init (char *cmd, char **argzp, size_t *argz_lenp,
                              char *extra_args[])
{
    char *extra;
    size_t extra_len;
    int e;
    char *argv[] = { cmd, "-i", "-", "--if", "xml",
                     "--of", "console", NULL };
    if (  (e = argz_create (argv, argzp, argz_lenp)) != 0
       || (e = argz_create (extra_args, &extra, &extra_len)) != 0)
        log_msg_exit ("argz_create: %s", strerror (e));

    /*  Append any extra args in av[] */
    if ((e = argz_append (argzp, argz_lenp, extra, extra_len)) != 0)
        log_msg_exit ("argz_append: %s", strerror (e));

    free (extra);
}

int argz_execp (char *argz, size_t argz_len)
{
    char *argv [argz_count (argz, argz_len) + 1];
    argz_extract (argz, argz_len, argv);
    return execvp (argv[0], argv);
}

FILE * argz_popen (char *argz, size_t argz_len, pid_t *pidptr)
{
    int pfds[2];
    pid_t pid;
    if (pipe (pfds) < 0)
        log_err_exit ("pipe");
    switch ((pid = fork ())) {
    case -1:
        log_err_exit ("fork");
    case  0:
        close (pfds[1]);
        dup2 (pfds[0], STDIN_FILENO);
        argz_execp (argz, argz_len);
        if (errno != ENOENT)
            log_err ("exec");
        exit (errno); /* So we can detect ENOENT.. Sorry */
    default:
        break;
    }
    close (pfds[0]);
    *pidptr = pid;
    return (fdopen (pfds[1], "w"));
}

/*
 *  Execute lstopo from user's PATH sending full topology XML over stdin.
 *   Pass any extra options along to lstopo(1).
 *
 *  If running lstopo fails with ENOENT, try lstopo-no-graphics.
 */
static int exec_lstopo (optparse_t *p, int ac, char *av[], const char *topo)
{
    int status;
    FILE *fp;
    char *argz;
    size_t argz_len;
    pid_t pid;
    const char *cmds[] = { "lstopo", "lstopo-no-graphics", NULL };
    const char **cp = cmds;

    /* Ignore SIGPIPE so we don't get killed when exec() fails */
    signal (SIGPIPE, SIG_IGN);

    /* Initialize argz with first command in cmds above: */
    lstopo_argz_init ((char *) *cp, &argz, &argz_len, av+1);

    while (true) {
        const char *next = *(cp+1);
        if (!(fp = argz_popen (argz, argz_len, &pid)))
            log_err_exit ("popen (lstopo)");
        fputs (topo, fp);
        fclose (fp);
        if (waitpid (pid, &status, 0) < 0)
            log_err_exit ("waitpid");

        /* Break out of loop if exec() was succcessful, failed with
         *  an error other than "File not found", or we ran out programs
         *  to try.
         */
        if (status == 0 || !next || WEXITSTATUS (status) != ENOENT)
            break;

        /* Replace previous cmd in argz with next command to try:
         */
        argz_replace (&argz, &argz_len, *(cp++), next, NULL);
    }

    return (status);
}

static int cmd_lstopo (optparse_t *p, int ac, char *av[])
{
    int status;
    struct hwloc_topo *t = hwloc_topo_create (p);
    assert (t != NULL);

    status = exec_lstopo (p, ac, av, hwloc_topo_topology (t));
    if (status) {
        if (WIFEXITED (status)) {
            if (WEXITSTATUS (status) == ENOENT)
                log_msg_exit ("Unable to find an lstopo variant to execute.");
            else
                log_msg_exit ("lstopo: Exited with %d", WEXITSTATUS (status));
        }
        if (WIFSIGNALED (status) && WTERMSIG (status) != SIGPIPE)
            log_msg_exit ("lstopo: %s%s", strsignal (WTERMSIG (status)),
                      WCOREDUMP (status) ? " (core dumped)" : "");
    }

    hwloc_topo_destroy (t);

    return (0);
}

static int cmd_topology (optparse_t *p, int ac, char *av[])
{
    struct hwloc_topo *t = hwloc_topo_create (p);
    puts (hwloc_topo_topology (t));
    hwloc_topo_destroy (t);
    return (0);
}

static int cmd_info (optparse_t *p, int ac, char *av[])
{
    struct hwloc_topo *t = hwloc_topo_create (p);
    hwloc_topology_t topo;

    if (hwloc_topology_init (&topo) < 0)
        log_msg_exit ("hwloc_topology_init");
    if (hwloc_topology_set_xmlbuffer (topo, t->topo, strlen (t->topo)) < 0)
        log_msg_exit ("hwloc_topology_set_xmlbuffer");
    if (hwloc_topology_load (topo) < 0)
        log_msg_exit ("hwloc_topology_load");

    int ncores = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_CORE);
    int npu    = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_PU);
    int nnodes = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_MACHINE);

    printf ("%d Machine%s, %d Cores, %d PUs\n",
            nnodes, nnodes > 1 ? "s" : "", ncores, npu);

    hwloc_topology_destroy (topo);
    hwloc_topo_destroy (t);
    return (0);
}

static void config_hwloc_paths (flux_t *h, const char *dirpath)
{
    uint32_t size, rank;
    const char *key_prefix = "config.resource.hwloc.xml";
    char key[64];
    char path[PATH_MAX];
    int n;

    if (flux_get_size (h, &size) < 0)
        log_err_exit ("flux_get_size");
    for (rank = 0; rank < size; rank++) {
        n = snprintf (key, sizeof (key), "%s.%"PRIu32, key_prefix, rank);
        assert (n < sizeof (key));
        if (dirpath == NULL) {
            /* Remove any per rank xml and reload default xml */
            if (kvs_unlink (h, key) < 0)
                log_err_exit ("kvs_unlink");
            continue;
        }
        n = snprintf (path, sizeof (path), "%s/%"PRIu32".xml", dirpath, rank);
        assert (n < sizeof (path));
        if (access (path, R_OK) < 0)
            log_err_exit ("%s", path);
        if (kvs_put_string (h, key, path) < 0)
            log_err_exit ("kvs_put_string");
    }
    if (kvs_commit (h, 0) < 0)
        log_err_exit ("kvs_commit");
}

static bool hwloc_reload_bool_value (const char *walk_topology)
{
    bool v = true;

    if (strcmp (walk_topology, "no") == 0
        || strcmp (walk_topology, "0") == 0
        || strcmp (walk_topology, "false") == 0)
        v = false;
    return (v);
}

static void request_hwloc_reload (flux_t *h, const char *nodeset,
                                  const char *walk_topology)
{
    flux_rpc_t *rpc;

    if (!walk_topology) {
        if (!(rpc = flux_rpcf_multi (h, "resource-hwloc.reload",
                                     nodeset, 0, "{}")))
            log_err_exit ("flux_rpcf_multi");
    }
    else {
        bool v = hwloc_reload_bool_value (walk_topology);

        if (!(rpc = flux_rpcf_multi (h, "resource-hwloc.reload", nodeset, 0,
                                     "{ s:b }", "walk_topology", v)))
            log_err_exit ("flux_rpcf_multi");
    }

    do {
        uint32_t nodeid = FLUX_NODEID_ANY;
        if (flux_rpc_get (rpc, NULL) < 0
                        || flux_rpc_get_nodeid (rpc, &nodeid)) {
            if (nodeid == FLUX_NODEID_ANY)
                log_err ("flux_rpc_get");
            else
                log_err ("rpc(%"PRIu32")", nodeid);
        }
    } while (flux_rpc_next (rpc) == 0);
    flux_rpc_destroy (rpc);
}

static int internal_hwloc_reload (optparse_t *p, int ac, char *av[])
{
    int n = optparse_option_index (p);
    const char *default_nodeset = "all";
    const char *nodeset = optparse_get_str (p, "rank", default_nodeset);
    const char *walk_topology = optparse_get_str (p, "walk-topology", NULL);
    char *dirpath = NULL;
    flux_t *h;

    if (!(h = builtin_get_flux_handle (p)))
        log_err_exit ("flux_open");
    if (av[n] && !(dirpath = realpath (av[n], NULL)))
        log_err_exit ("%s", av[n]);

    config_hwloc_paths (h, dirpath);
    request_hwloc_reload (h, nodeset, walk_topology);

    free (dirpath);
    flux_close (h);
    return (0);
}

int cmd_hwloc (optparse_t *p, int ac, char *av[])
{
    log_init ("flux-hwloc");
    if (optparse_run_subcommand (p, ac, av) != OPTPARSE_SUCCESS)
        exit (1);
    return (0);
}

static struct optparse_option reload_opts[] = {
    { .name = "rank",  .key = 'r',  .has_arg = 1,
      .usage = "Target specified nodeset, or \"all\" (default)", },
    { .name = "walk-topology",  .key = 't',  .has_arg = 1,
      .arginfo = "yes/no",
      .usage = "Force enable/disable topology walk for reload", },
    OPTPARSE_TABLE_END,
};

static struct optparse_subcommand hwloc_subcmds[] = {
    { "reload",
      "[OPTIONS] [DIR]",
      "Reload hwloc XML, optionally from DIR/<rank>.xml files",
      internal_hwloc_reload,
      0,
      reload_opts,
    },
   { "lstopo",
      "[lstopo-OPTIONS]",
      "Show hwloc topology of the system",
      cmd_lstopo,
      OPTPARSE_SUBCMD_SKIP_OPTS,
      NULL,
    },
    { "topology",
      NULL,
      "Dump system topology XML to stdout",
      cmd_topology,
      0,
      NULL,
    },
    { "info",
      NULL,
      "Short-form dump of instance resources",
      cmd_info,
      0,
      NULL
    },
    OPTPARSE_SUBCMD_END,
};

int subcommand_hwloc_register (optparse_t *p)
{
    optparse_t *c;
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "hwloc", cmd_hwloc, NULL,
                                 "Control/query resource-hwloc service",
                                 0, NULL);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    c = optparse_get_subcommand (p, "hwloc");
    if ((e = optparse_reg_subcommands (c, hwloc_subcmds)) != OPTPARSE_SUCCESS)
        return (-1);
    return (e == OPTPARSE_SUCCESS ? 0 : -1);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
