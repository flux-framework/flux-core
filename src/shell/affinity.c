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
#include "src/common/librlist/rhwloc.h"

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
        free (cpusetp);
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
    flux_error_t error;
    hwloc_cpuset_t cpuset = rhwloc_cores_to_cpuset (sa->topo,
                                                    cores,
                                                    &error);
    if (!cpuset)
        shell_log_error ("affinity: failed to get cpuset for cores: %s: %s",
                         cores,
                         error.text);
    return cpuset;
}

static char *cpuset_to_string (hwloc_cpuset_t cpuset)
{
    char *result = NULL;
    if (hwloc_bitmap_list_asprintf (&result, cpuset) < 0)
        shell_log_errno ("hwloc_bitmap_list_asprintf");
    return result;
}

/*  Return the cpusets of the `ntasks` tasks sharing a slot with cores
 *   `cores`, distributed over the slot cores with distribute_tasks(), as
 *   tasks are over all shell cores when R has no slots. Since
 *   distribute_tasks() restricts the topology, use a copy.
 */
static hwloc_cpuset_t *distribute_slot (struct shell_affinity *sa,
                                        const char *cores,
                                        int ntasks)
{
    hwloc_topology_t topo;
    hwloc_cpuset_t cpuset;
    hwloc_cpuset_t *result = NULL;

    if (!(cpuset = shell_affinity_get_cpuset (sa, cores)))
        return NULL;
    /* hwloc_distrib() gives one task all of the slot cores, so skip the
     * topology copy.
     */
    if (ntasks == 1) {
        if (!(result = calloc (1, sizeof (*result)))) {
            hwloc_bitmap_free (cpuset);
            return NULL;
        }
        result[0] = cpuset;
        return result;
    }
    if (hwloc_topology_dup (&topo, sa->topo) < 0)
        shell_log_errno ("hwloc_topology_dup");
    else {
        result = distribute_tasks (topo, cpuset, ntasks);
        hwloc_topology_destroy (topo);
    }
    hwloc_bitmap_free (cpuset);
    return result;
}

/*  The cpusets of the tasks sharing a slot, as computed by
 *   distribute_slot(). Slots with the same cores and number of tasks
 *   have the same cpusets, so these are computed once and cached.
 */
struct slot_cpusets {
    const char *cores;
    int ntasks;
    hwloc_cpuset_t *cpusets;
};

struct slot_cache {
    struct slot_cpusets *entries;
    int count;
};

static void slot_cache_clear (struct slot_cache *cache)
{
    for (int i = 0; i < cache->count; i++)
        cpuset_array_destroy (cache->entries[i].cpusets,
                              cache->entries[i].ntasks);
    free (cache->entries);
}

/*  Return the cpusets of the `ntasks` tasks sharing a slot with cores
 *   `cores`, from `cache` if possible. `cache` has room for one entry
 *   per local task, which is the most distinct slots there can be.
 */
static hwloc_cpuset_t *slot_cache_get (struct shell_affinity *sa,
                                       struct slot_cache *cache,
                                       const char *cores,
                                       int ntasks)
{
    struct slot_cpusets *c;

    for (int i = 0; i < cache->count; i++) {
        c = &cache->entries[i];
        if (streq (c->cores, cores) && c->ntasks == ntasks)
            return c->cpusets;
    }
    c = &cache->entries[cache->count];
    if (!(c->cpusets = distribute_slot (sa, cores, ntasks))) {
        shell_log_error ("failed to distribute %d tasks over slot cores %s",
                         ntasks,
                         cores);
        return NULL;
    }
    c->cores = cores;
    c->ntasks = ntasks;
    cache->count++;
    return c->cpusets;
}

/*  Return a per-task cpuset array that binds each task to the cores of
 *   its RFC 20 slot. Tasks that share a slot are distributed over the slot
 *   cores, as tasks are over all cores of the shell when R has no slots.
 */
static hwloc_cpuset_t *slot_cpusets (flux_shell_t *shell,
                                     struct shell_affinity *sa)
{
    hwloc_cpuset_t *cpusets;
    struct slot_cache cache = { 0 };
    flux_shell_task_t *task;

    cpusets = calloc (sa->ntasks, sizeof (*cpusets));
    cache.entries = calloc (sa->ntasks, sizeof (*cache.entries));
    if (!cpusets || !cache.entries)
        goto error;
    for (task = flux_shell_task_first (shell);
         task != NULL;
         task = flux_shell_task_next (shell)) {
        int localid;
        const char *cores;
        int ntasks;
        int task_index;
        hwloc_cpuset_t *shared;

        /* The task is task `task_index` of the `ntasks` tasks sharing a
         * slot with cores `cores`.
         */
        if (flux_shell_task_info_unpack (task,
                                         "{s:i s:{s:s s:i s:i}}",
                                         "localid", &localid,
                                         "slot",
                                           "cores", &cores,
                                           "ntasks", &ntasks,
                                           "task_index", &task_index) < 0
            || localid < 0
            || localid >= sa->ntasks
            || task_index < 0
            || task_index >= ntasks) {
            shell_log_error ("failed to get task slot");
            goto error;
        }
        if (!(shared = slot_cache_get (sa, &cache, cores, ntasks))
            || !(cpusets[localid] = hwloc_bitmap_dup (shared[task_index])))
            goto error;
    }
    slot_cache_clear (&cache);
    return cpusets;
error:
    slot_cache_clear (&cache);
    cpuset_array_destroy (cpusets, sa->ntasks);
    return NULL;
}

bool shell_has_slots (flux_shell_t *shell)
{
    json_t *slots = NULL;

    if (flux_shell_rank_info_unpack (shell, -1, "{s?o}", "slots", &slots) < 0)
        return false;
    return slots != NULL;
}

static void print_pertask_cpusets (flux_shell_t *shell,
                                   struct shell_affinity *sa)
{
    char *s;
    int i;
    int rank;
    flux_shell_task_t *task;

    if (sa->pertask) {
        task = flux_shell_task_first (shell);
        while (task) {
            if (flux_shell_task_info_unpack (task,
                                             "{s:i s:i}",
                                             "localid", &i,
                                             "rank", &rank) < 0)
                shell_log_errno ("flux_shell_task_info_unpack");
            else if ((s = cpuset_to_string (sa->pertask[i]))) {
                shell_log ("task %d: cpus: %s", rank, s);
                free (s);
            }
            task = flux_shell_task_next (shell);
        }
    }
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
                                         struct shell_affinity *sa,
                                         bool dry_run)
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
     *   O/w hwloc CPU binding will not work (skip if dry-run)
     */
    if (!dry_run
        && hwloc_topology_set_flags (sa->topo,
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
static struct shell_affinity *shell_affinity_create (flux_shell_t *shell,
                                                     bool dry_run)
{
    struct shell_affinity *sa = calloc (1, sizeof (*sa));
    if (!sa)
        return NULL;
    if (shell_affinity_topology_init (shell, sa, dry_run) < 0)
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

static bool consume_option (const char **optp, const char *name)
{
    const char *p = *optp;
    if (strstarts (*optp, name)) {
        /* forward past name and any ',' */
        p += strlen (name);
        if (*p == ',')
            p++;
        *optp = p;
        return true;
    }
    return false;
}

/*  Parse any shell 'cpu-affinity' and return true if shell affinity
 *   is enabled. Return any string option setting in resultp.
 *  By default, affinity is enabled unless cpu-affinity="off".
 */
static bool affinity_getopt (flux_shell_t *shell,
                             const char **resultp,
                             bool *verbose,
                             bool *dry_run)
{
    int rc;
    const char *opt;

    *verbose = false;
    *dry_run = false;

    /* Default if not set is "on" */
    *resultp = "on";
    rc = flux_shell_getopt_unpack (shell, "cpu-affinity", "s", &opt);
    if (rc == 0) {
        return true;
    }
    else if (rc < 0) {
        shell_warn ("cpu-affinity: invalid option: %s", *resultp);
        return true;
    }
    /* Check for options dry-run and verbose, both of which must come at
     * the beginning of the option string and can optionally be followed
     * by a comma.
     */
    while (true) {
        if (consume_option (&opt, "dry-run"))
            *dry_run = true;
        else if (consume_option (&opt, "verbose"))
            *verbose = true;
        else
            break;
    }
    *resultp = opt;
    if (streq (*resultp, "off"))
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
    char *s;
    const char *option;
    bool dry_run;
    bool verbose;
    struct shell_affinity *sa = NULL;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return shell_log_errno ("flux_plugin_get_shell");
    if (!affinity_getopt (shell, &option, &verbose, &dry_run)) {
        shell_debug ("disabling affinity due to cpu-affinity=off");
        return 0;
    }
    if (!(sa = shell_affinity_create (shell, dry_run)))
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
    if ((dry_run || verbose)
        && (s = cpuset_to_string (sa->cpuset))) {
        shell_log ("cpus: %s", s);
        free (s);
    }
    if (!dry_run && wrap_hwloc_set_cpubind (sa->topo, sa->cpuset, 0) < 0)
        return shell_log_errno ("hwloc_set_cpubind");

    /*  If cpu-affinity=per-task, then bind each task to the cores of
     *   its slot if the scheduler assigned slots in R. Otherwise,
     *   distribute ntasks over whatever resources to which the shell
     *   is now bound (from above).
     *  Set a 'task.exec' callback to actually make the per-task binding.
     */
    if (streq (option, "per-task")) {
        if (shell_has_slots (shell)) {
            if (!(sa->pertask = slot_cpusets (shell, sa)))
                return shell_log_errno ("failed to bind tasks to slots");
        }
        else if (!(sa->pertask = distribute_tasks (sa->topo,
                                                   sa->cpuset,
                                                   sa->ntasks)))
            shell_log_errno ("distribute_tasks failed");
    }
    else if (strstarts (option, "map:")) {
        if (!(sa->pertask = parse_cpuset_list (option+4, sa->ntasks)))
            return -1;
    }
    if (!dry_run
        && sa->pertask
        && flux_plugin_add_handler (p,
                                    "task.exec",
                                    task_affinity,
                                    sa) < 0)
            shell_log_errno ("failed to add task.exec handler");

    if (dry_run || verbose)
        print_pertask_cpusets (shell, sa);

    return 0;
}

struct shell_builtin builtin_affinity = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = affinity_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
