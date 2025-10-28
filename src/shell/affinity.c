/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* builtin cpu-affinity processing
 */
#define FLUX_SHELL_PLUGIN_NAME "cpu-affinity"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <hwloc.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "ccan/str/str.h"

#include "builtins.h"

struct shell_affinity {
    hwloc_topology_t topo;
    int ntasks;
    const char *cores;
    hwloc_cpuset_t cpuset;
    hwloc_cpuset_t *pertask;
};

int wrap_hwloc_set_cpubind (hwloc_topology_t topo, hwloc_cpuset_t set, int i)
{
    int ret = 0;
#if !defined (__APPLE__)
    ret = hwloc_set_cpubind (topo, set, i);
#endif
    return ret;
}

void cpuset_array_destroy (hwloc_cpuset_t *set, int size)
{
    if (set) {
        for (int i = 0; i < size; i++) {
            if (set[i] != NULL)
                hwloc_bitmap_free (set[i]);
        }
        free (set);
    }
}

hwloc_cpuset_t *cpuset_array_create (int size)
{
    hwloc_cpuset_t *set = calloc (size, sizeof (hwloc_cpuset_t));
    if (!set)
        return NULL;
    for (int i = 0; i < size; i++)
        if (!(set[i] = hwloc_bitmap_alloc ()))
            goto error;
    return set;
error:
    cpuset_array_destroy (set, size);
    return NULL;
}

/*  Run hwloc_topology_restrict() with common flags for this module.
 */
static int topology_restrict (hwloc_topology_t topo, hwloc_cpuset_t set)
{
    if (hwloc_topology_restrict (topo, set, 0) < 0)
        return (-1);
    return (0);
}


/*  Parse a list of hwloc bitmap strings in list, bitmask, or taskset
 *  form and return an allocated hwloc_cpuset_t array of size ntasks,
 *  filled with the resulting bitmasks. If ntasks is greater than the number
 *  of provided cpusets, then cpusets are reused as necessary.
 */
hwloc_cpuset_t *parse_cpuset_list (const char *setlist,
                                   int ntasks)
{
    char *copy = NULL;
    char *s, *arg, *sptr = NULL;
    int index, i = 0;
    hwloc_cpuset_t *cpusets = NULL;

    if (!(cpusets = cpuset_array_create (ntasks))
        || !(copy = strdup (setlist))) {
        shell_log_errno ("out of memory");
        goto err;
    }

    s = copy;
    while ((arg = strtok_r (s, ";", &sptr)) && i < ntasks) {
        int rc;
        if (strstarts (arg, "0x")) {
            /*  If string starts with 0x then parse as a bitmask. If a
             *  comma is present in the string, then this is likely a
             *  hwloc-style bitmap string, otherwise, try the taskset
             *  style bitmaps, which are simpler.
             */
            if (strchr (arg, ','))
                rc = hwloc_bitmap_sscanf (cpusets[i], arg);
            else
                rc = hwloc_bitmap_taskset_sscanf (cpusets[i], arg);
        }
        else {
            /*  O/w, attempt parse string as a hwloc list-style bitmap:
             */
            rc = hwloc_bitmap_list_sscanf (cpusets[i], arg);
        }

        if (rc < 0 || hwloc_bitmap_weight (cpusets[i]) <= 0) {
            shell_log_error ("cpuset %s contains no cores or is invalid",
                             arg);
            goto err;
        }
        s = NULL;
        i++;
    }
    if (i == 0) {
        shell_log_error ("no cpusets found in affinity list %s", setlist);
        goto err;
    }

    /*  If not all tasks were assigned cpusets, then continue, reusing
     *  cpusets as necessary.
     */
    index = 0;
    for (; i < ntasks; i++) {
        (void) hwloc_bitmap_copy (cpusets[i], cpusets[index]);
        if (++index > ntasks)
            index = 0;
    }
    free (copy);
    return cpusets;
err:
    cpuset_array_destroy (cpusets, ntasks);
    free (copy);
    return NULL;
}

/*  Distribute ntasks over the topology 'topo', restricted to the
 *   cpuset give in 'cset' if non-NULL.
 *
 *  Returns a hwloc_cpuset_t array of size ntasks.
 */
static hwloc_cpuset_t *distribute_tasks (hwloc_topology_t topo,
                                         hwloc_cpuset_t cset,
                                         int ntasks)
{
    hwloc_obj_t *roots;
    hwloc_cpuset_t *cpusetp = NULL;
    int cores;
    int depth;

    /* restrict topology to current cpuset */
    if (cset && topology_restrict (topo, cset) < 0) {
        shell_log_errno ("topology_restrict failed");
        return NULL;
    }
    /* create cpuset array for ntasks */
    if (!(cpusetp = calloc (ntasks, sizeof (hwloc_cpuset_t))))
        return NULL;

    depth = hwloc_get_type_depth (topo, HWLOC_OBJ_CORE);
    cores = hwloc_get_nbobjs_by_depth (topo, depth);
    if (cores <= 0 || !(roots = calloc (cores, sizeof (*roots)))) {
        shell_log_error ("failed to allocate %d roots for hwloc distrib",
                         cores);
        return NULL;
    }

    for (int i = 0; i < cores; i++)
        roots[i] = hwloc_get_obj_by_depth (topo, depth, i);

    shell_trace ("distributing %d tasks across %d cores", ntasks, cores);

    /* NB: hwloc_distrib() will alloc ntasks cpusets in cpusetp, which
     *     later need to be destroyed with hwloc_bitmap_free().
     */
    hwloc_distrib (topo, roots, cores, cpusetp, ntasks, depth, 0);

    free (roots);
    return (cpusetp);
}

/*  Return the cpuset that is the union of cpusets contained in "cores" list.
 */
static hwloc_cpuset_t shell_affinity_get_cpuset (struct shell_affinity *sa,
                                                 const char *cores)
{
    int depth, i;
    hwloc_cpuset_t coreset = NULL;
    hwloc_cpuset_t resultset = NULL;

    if (!(coreset = hwloc_bitmap_alloc ())
        || !(resultset = hwloc_bitmap_alloc ())) {
        shell_log_errno ("hwloc_bitmap_alloc");
        goto err;
    }

    /*  Parse cpus as bitmap list
     */
    if (hwloc_bitmap_list_sscanf (coreset, cores) < 0) {
        shell_log_error ("affinity: failed to read core list: %s", cores);
        goto err;
    }

    /*  Find depth of type core in this topology:
     */
    depth = hwloc_get_type_depth (sa->topo, HWLOC_OBJ_CORE);
    if (depth == HWLOC_TYPE_DEPTH_UNKNOWN
        || depth == HWLOC_TYPE_DEPTH_MULTIPLE) {
        shell_log_error ("hwloc_get_type_depth (CORE) returned nonsense");
        goto err;
    }

    /*  Get the union of all allocated cores' cpusets into sa->cpuset
     */
    i = hwloc_bitmap_first (coreset);
    while (i >= 0) {
        hwloc_obj_t core = hwloc_get_obj_by_depth (sa->topo, depth, i);
        if (!core) {
            shell_log_error ("affinity: core%d not in topology", i);
            goto err;
        }
        if (!core->cpuset) {
            shell_log_error ("affinity: core%d cpuset is null", i);
            goto err;
        }
        hwloc_bitmap_or (resultset, resultset, core->cpuset);
        i = hwloc_bitmap_next (coreset, i);
    }
    hwloc_bitmap_free (coreset);
    return resultset;
err:
    if (coreset)
        hwloc_bitmap_free (coreset);
    if (resultset)
        hwloc_bitmap_free (resultset);
    return NULL;
}

static void shell_affinity_destroy (void *arg)
{
    struct shell_affinity *sa = arg;
    if (sa->topo)
        hwloc_topology_destroy (sa->topo);
    if (sa->cpuset)
        hwloc_bitmap_free (sa->cpuset);
    cpuset_array_destroy (sa->pertask, sa->ntasks);
    free (sa);
}

/*  Initialize topology object for affinity processing.
 */
static int shell_affinity_topology_init (flux_shell_t *shell,
                                         struct shell_affinity *sa)
{
    const char *xml;

    /*  Fetch hwloc XML cached in job shell to avoid heavyweight
     *   hwloc topology load (Issue #4365)
     */
    if (flux_shell_get_hwloc_xml (shell, &xml) < 0)
        return shell_log_errno ("failed to unpack hwloc object");

    if (hwloc_topology_init (&sa->topo) < 0)
        return shell_log_errno ("hwloc_topology_init");

    if (hwloc_topology_set_xmlbuffer (sa->topo, xml, strlen (xml)) < 0)
        return shell_log_errno ("hwloc_topology_set_xmlbuffer");

    /*  Tell hwloc that our XML loaded topology is from this system,
     *   O/w hwloc CPU binding will not work.
     */
    if (hwloc_topology_set_flags (sa->topo,
                                  HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM) < 0)
        return shell_log_errno ("hwloc_topology_set_flags");

    if (hwloc_topology_load (sa->topo) < 0)
        return shell_log_errno ("hwloc_topology_load");
    return 0;
}

/*  Create shell affinity context, including reading in hwloc
 *   topology, gathering number of local tasks and assigned core list,
 *   and getting the resulting cpuset for the entire shell.
 */
static struct shell_affinity *shell_affinity_create (flux_shell_t *shell)
{
    struct shell_affinity *sa = calloc (1, sizeof (*sa));
    if (!sa)
        return NULL;
    if (shell_affinity_topology_init (shell, sa) < 0)
        goto err;
    if (flux_shell_rank_info_unpack (shell,
                                     -1,
                                     "{s:i s:{s:s}}",
                                     "ntasks", &sa->ntasks,
                                     "resources",
                                       "cores", &sa->cores) < 0) {
        shell_log_errno ("flux_shell_rank_info_unpack");
        goto err;
    }
    return sa;
err:
    shell_affinity_destroy (sa);
    return NULL;
}

/*  Parse any shell 'cpu-affinity' and return true if shell affinity
 *   is enabled. Return any string option setting in resultp.
 *  By default, affinity is enabled unless cpu-affinity="off".
 */
static bool affinity_getopt (flux_shell_t *shell, const char **resultp)
{
    int rc;
    /* Default if not set is "on" */
    *resultp = "on";
    rc = flux_shell_getopt_unpack (shell, "cpu-affinity", "s", resultp);
    if (rc == 0) {
        return true;
    }
    else if (rc < 0) {
        shell_warn ("cpu-affinity: invalid option: %s", *resultp);
        return true;
    }
    else if (streq (*resultp, "off"))
        return false;
    return true;
}


/*  Return task id for a shell task
 */
static int flux_shell_task_getid (flux_shell_task_t *task)
{
    int id = -1;
    if (flux_shell_task_info_unpack (task, "{s:i}", "localid", &id) < 0)
        return -1;
    return id;
}

/*  Return the current task id when running in task.* context.
 */
static int get_taskid (flux_plugin_t *p)
{
    flux_shell_t *shell;
    flux_shell_task_t *task;

    if (!(shell = flux_plugin_get_shell (p)))
        return -1;
    if (!(task = flux_shell_current_task (shell)))
        return -1;
    return flux_shell_task_getid (task);
}

static int task_affinity (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    struct shell_affinity *sa = data;
    int i = get_taskid (p);
    if (sa->pertask)
        wrap_hwloc_set_cpubind (sa->topo, sa->pertask[i], 0);
    shell_affinity_destroy (sa);
    return 0;
}

static int affinity_init (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    const char *option;
    struct shell_affinity *sa = NULL;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return shell_log_errno ("flux_plugin_get_shell");
    if (!affinity_getopt (shell, &option)) {
        shell_debug ("disabling affinity due to cpu-affinity=off");
        return 0;
    }
    if (!(sa = shell_affinity_create (shell)))
        return shell_log_errno ("shell_affinity_create");

    /*  Attempt to get cpuset union of all allocated cores. If this
     *   fails, then it might be because the allocated cores exceeds
     *   the real cores available on this machine, so just log an
     *   informational message and skip setting affinity.
     */
    if (!(sa->cpuset = shell_affinity_get_cpuset (sa, sa->cores))) {
        shell_warn ("unable to get cpuset for cores %s. Disabling affinity",
                    sa->cores);
        return 0;
    }
    if (flux_plugin_aux_set (p, "affinity", sa, shell_affinity_destroy) < 0) {
        shell_affinity_destroy (sa);
        return -1;
    }
    if (wrap_hwloc_set_cpubind (sa->topo, sa->cpuset, 0) < 0)
        return shell_log_errno ("hwloc_set_cpubind");

    /*  If cpu-affinity=per-task, then distribute ntasks over whatever
     *   resources to which the shell is now bound (from above)
     *  Set a 'task.exec' callback to actually make the per-task binding.
     */
    if (streq (option, "per-task")) {
        if (!(sa->pertask = distribute_tasks (sa->topo,
                                              sa->cpuset,
                                              sa->ntasks)))
            shell_log_errno ("distribute_tasks failed");
    }
    else if (strstarts (option, "map:")) {
        if (!(sa->pertask = parse_cpuset_list (option+4, sa->ntasks)))
            return -1;
    }
    if (sa->pertask
        && flux_plugin_add_handler (p,
                                    "task.exec",
                                    task_affinity,
                                    sa) < 0)
            shell_log_errno ("failed to add task.exec handler");

    return 0;
}

struct shell_builtin builtin_affinity = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = affinity_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
