/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <flux/hostlist.h>
#include <flux/shell.h>

#include "jansson.h"

#include "jobspec.h"

/*  PALS --- interface with HPE/Cray's PMI.
 *
 * To support Cray PMI, the launcher must perform these tasks:
 * 1. Assign an apid to the application
 * (arbitrary string, unique per-application)
 * 2. On each compute node, create a spool directory
 * (in this case, FLUX_JOB_TMPDIR) owned by the application's user
 * 3. On each compute node, write an apinfo file in
 * the spool directory.
 * 4. Set environment variables for each spawned process (listed below)
 * 5. Remove the spool directory on each compute node when
 * the application is complete
 *
 * These environment variables should be set for each process:
 * PALS_APID - Application ID (arbitrary string, mostly used for logging)
 * PALS_APINFO - Full path to the apinfo file
 * PALS_RANKID - Global rank ID for this process
 * PALS_NODEID - Node index for this process
 * (e.g. head compute node is 0, next compute node is 1, etc)
 * PALS_SPOOL_DIR - Application-specific directory for keeping runtime files
 * PMI_CONTROL_PORT - Port number for PMI to bind on each compute node.
 * Must be different for each concurrent application running on the
 * same node(s).
 */

/* Application file format version */
#define PALS_APINFO_VERSION 1

/* File header structure */
typedef struct {
    int version;                // Set to PALS_APINFO_VERSION
    size_t total_size;          // Size of the whole file in bytes
    size_t comm_profile_size;   // sizeof(pals_comm_profile_t)
    // offset from beginning of file to the first comm_profile_t
    size_t comm_profile_offset;
    // number of comm_profile_t (not used yet, set to 0)
    int ncomm_profiles;
    size_t cmd_size;            // sizeof(pals_cmd_t)
    // offset from beginning of file to the first pals_cmd_t
    size_t cmd_offset;
    int ncmds;                  // number of commands (MPMD programs)
    size_t pe_size;             // sizeof(pals_pe_t)
    // offset from beginning of file to the first pals_pe_t
    size_t pe_offset;
    int npes;                   // number of PEs (processes/ranks)
    size_t node_size;           // sizeof(pals_node_t)
    // offset from beginning of file to the first pals_node_t
    size_t node_offset;
    int nnodes;                 // number of nodes
    size_t nic_size;            // sizeof(pals_nic_t)
    // offset from beginning of file to the first pals_nic_t
    size_t nic_offset;
    int nnics;                  // number of NICs (not used yet, set to 0)
} pals_header_t;

/* Network communication profile structure */
typedef struct {
    char tokenid[40];    /* Token UUID */
    int vni;             /* VNI associated with this token */
    int vlan;            /* VLAN associated with this token */
    int traffic_classes; /* Bitmap of allowed traffic classes */
} pals_comm_profile_t;

/* MPMD command information structure */
typedef struct {
    int npes;         /* Number of tasks in this command */
    int pes_per_node; /* Number of tasks per node */
    int cpus_per_pe;  /* Number of CPUs per task */
} pals_cmd_t;

/* PE (i.e. task) information structure */
typedef struct {
    int localidx; /* Node-local PE index */
    int cmdidx;   /* Command index for this PE */
    int nodeidx;  /* Node index this PE is running on */
} pals_pe_t;

/* Node information structure */
typedef struct {
    int nid;           /* Node ID */
    char hostname[64]; /* Node hostname */
} pals_node_t;

/* NIC address type */
typedef enum {
    PALS_ADDR_IPV4,
    PALS_ADDR_IPV6,
    PALS_ADDR_MAC
} pals_address_type_t;

/* NIC information structure */
typedef struct {
    int nodeidx;                      /* Node index this NIC belongs to */
    pals_address_type_t address_type; /* Address type for this NIC */
    char address[40];                 /* Address of this NIC */
} pals_nic_t;

static int safe_write (int fd, const void *buf, size_t size)
{
    ssize_t rc;
    while (size > 0) {
        rc = write (fd, buf, size);
        if (rc < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            return -1;
        } else {
            buf += rc;
            size -= rc;
        }
    }
    return 0;
}

/*
 * Return an array of initialized pals_pe_t structures.
 * 'task_counts' should be an array of length 'nnodes' specifying
 * how many tasks are on each node in the job.
 * 'tids' should be a 2D ragged array giving the job ranks for each
 * node in the job.
 */
static pals_pe_t *setup_pals_pes (int ntasks,
                                  int nnodes,
                                  int *task_counts,
                                  int **tids)
{
    pals_pe_t *pes = NULL;
    int nodeidx, localidx, taskid;

    if (!(pes = calloc (ntasks, sizeof (pals_pe_t)))) {
        return NULL;
    }
    for (nodeidx = 0; nodeidx < nnodes;
         nodeidx++) {  // for each node identifier nodeidx ...
        for (localidx = 0; localidx < task_counts[nodeidx];
             localidx++) { // for each task within that node
            // get the global task ID of that task
            taskid = tids[nodeidx][localidx];
            if (taskid >= ntasks) {
                shell_log_error ("taskid %d (on node %d) >= ntasks %d",
                                 taskid,
                                 nodeidx,
                                 ntasks);
                free (pes);
                return NULL;
            }
            pes[taskid].nodeidx = nodeidx;
            pes[taskid].localidx = localidx;
            pes[taskid].cmdidx = 0;
        }
    }
    return pes;
}

/*
 * Initialize a pals_cmd_t.
 */
static void setup_pals_cmd (pals_cmd_t *cmd,
                                    int ntasks,
                                    int nnodes,
                                    int cores_per_task,
                                    int *task_counts)
{
    int max_tasks_per_node = 1;

    cmd->npes = ntasks;
    cmd->cpus_per_pe = cores_per_task;
    for (int i = 0; i < nnodes; ++i)
    {
        max_tasks_per_node = max_tasks_per_node > task_counts[i] \
            ? max_tasks_per_node : task_counts[i];
    }
    cmd->pes_per_node = max_tasks_per_node;
}

/*
 * Fill in the apinfo header.
 */
static void build_header (pals_header_t *hdr, int ncmds, int npes, int nnodes)
{
    size_t offset = sizeof (pals_header_t);

    memset (hdr, 0, sizeof (pals_header_t));
    hdr->version = PALS_APINFO_VERSION;

    hdr->comm_profile_size = sizeof (pals_comm_profile_t);
    hdr->comm_profile_offset = offset;
    hdr->ncomm_profiles = 0;
    offset += hdr->comm_profile_size * hdr->ncomm_profiles;

    hdr->cmd_size = sizeof (pals_cmd_t);
    hdr->cmd_offset = offset;
    hdr->ncmds = ncmds;
    offset += hdr->cmd_size * hdr->ncmds;

    hdr->pe_size = sizeof (pals_pe_t);
    hdr->pe_offset = offset;
    hdr->npes = npes;
    offset += hdr->pe_size * hdr->npes;

    hdr->node_size = sizeof (pals_node_t);
    hdr->node_offset = offset;
    hdr->nnodes = nnodes;
    offset += hdr->node_size * hdr->nnodes;

    hdr->nic_size = sizeof (pals_nic_t);
    hdr->nic_offset = offset;
    hdr->nnics = 0;
    offset += hdr->nic_size * hdr->nnics;

    hdr->total_size = offset;
}

/*
 * Write the job's hostlist to the file.
 */
static int write_pals_nodes (int fd, json_t *nodelist_array)
{
    size_t index;
    int node_index = 0;
    json_t *value;
    struct hostlist *hlist;
    const char *entry;
    pals_node_t node;

    if (!(hlist = hostlist_create ())) {
        return -1;
    }
    json_array_foreach (nodelist_array, index, value)
    {
        if (!(entry = json_string_value (value))
            || hostlist_append (hlist, entry) < 0) {
            hostlist_destroy (hlist);
            return -1;
        }
    }
    entry = hostlist_first (hlist);
    while (entry) {
        node.nid = node_index++;
        if (snprintf (node.hostname, sizeof node.hostname, "%s", entry)
                >= sizeof node.hostname
            || safe_write (fd, &node, sizeof (pals_node_t)) < 0) {
            hostlist_destroy (hlist);
            return -1;
        }
        entry = hostlist_next (hlist);
    }
    hostlist_destroy (hlist);
    return 0;
}

/*
 * Return the number of job tasks assigned to each shell rank.
 */
static int *get_task_counts (flux_shell_t *shell, int shell_size)
{
    int *task_counts;
    int i;

    if (!(task_counts = malloc (shell_size * sizeof shell_size))) {
        return NULL;
    }
    for (i = 0; i < shell_size; ++i) {
        if (flux_shell_rank_info_unpack (shell,
                                         i,
                                         "{s:i}",
                                         "ntasks",
                                         &task_counts[i]) < 0) {
            free (task_counts);
            return NULL;
        }
    }
    return task_counts;
}

/*
 * Return the job ranks assigned to each shell rank.
 * 'task_counts' should be an array of length 'shell_size' specifying
 * how many tasks are on each node in the job.
 */
static int **get_task_ids (int *task_counts, int shell_size)
{
    int **task_ids;
    int shell_rank, j;
    int curr_task_id = 0;

    if (!(task_ids = malloc (shell_size * sizeof task_counts))) {
        return NULL;
    }
    for (shell_rank = 0; shell_rank < shell_size; ++shell_rank) {
        if (!(task_ids[shell_rank] =
                  malloc (task_counts[shell_rank] * sizeof task_counts))) {
            for (j = 0; j < shell_rank; ++j) {
                free (task_ids[shell_rank]);
            }
            free (task_ids);
            return NULL;
        }
        for (j = 0; j < task_counts[shell_rank]; ++j) {
            task_ids[shell_rank][j] = curr_task_id++;
        }
    }
    return task_ids;
}

/*
 * Write the application information file
 */
static int create_apinfo (const char *apinfo_path, flux_shell_t *shell)
{
    int fd = -1, ret = 0, ntasks = 0;
    pals_header_t hdr;
    pals_cmd_t cmd;
    pals_pe_t *pes = NULL;
    int shell_size, cores_per_task = 1;
    int *task_counts = NULL, **task_ids = NULL;
    json_t *nodelist_array;

    // Get shell size and hostlist
    if (flux_shell_info_unpack (shell,
                                "{s:i, s:{s:{s:o}}}",
                                "size",
                                &shell_size,
                                "R",
                                  "execution",
                                    "nodelist",
                                    &nodelist_array)
            < 0
        || !json_is_array (nodelist_array)
        || !(task_counts = get_task_counts (shell, shell_size))
        || !(task_ids = get_task_ids (task_counts, shell_size))) {
        goto error;
    }
    for (int i = 0; i < shell_size; ++i) {  // count total job tasks
        ntasks += task_counts[i];
    }

    // Gather the header, pes, and cmds structs
    build_header (&hdr, 1, ntasks, shell_size);
    setup_pals_cmd (&cmd, ntasks, shell_size, cores_per_task, task_counts);
    if (!(pes = setup_pals_pes (ntasks, shell_size, task_counts, task_ids))) {
        goto error;
    }

    // Write the header, cmds, pes, and nodes structs
    if ((fd = creat (apinfo_path, S_IRUSR | S_IWUSR)) == -1
        || safe_write (fd, &hdr, sizeof (pals_header_t)) < 0
        || safe_write (fd, &cmd, (hdr.ncmds * sizeof (pals_cmd_t))) < 0
        || safe_write (fd, pes, (hdr.npes * sizeof (pals_pe_t))) < 0
        || write_pals_nodes (fd, nodelist_array) < 0
        || fsync (fd) == -1) {
        shell_log_errno ("Couldn't write apinfo to disk");
        goto error;
    }

cleanup:

    if (task_counts)
        free (task_counts);
    if (task_ids) {
        for (int i = 0; i < shell_size; ++i) {
            free (task_ids[i]);
        }
        free (task_ids);
    }
    if (pes)
        free (pes);
    close (fd);
    return ret;

error:

    ret = -1;
    goto cleanup;
}

/*
 * Set job-wide environment variables for LibPALS
 */
static int set_environment (flux_shell_t *shell, const char *apinfo_path)
{
    int rank = -1;
    json_int_t jobid;
    const char *tmpdir;

    if (flux_shell_info_unpack (shell,
                                "{s:i, s:I}",
                                "rank",
                                &rank,
                                "jobid",
                                &jobid) < 0
        || flux_shell_setenvf (shell, 1, "PALS_NODEID", "%i", rank) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "PALS_APID",
                               "%" JSON_INTEGER_FORMAT,
                               jobid) < 0
        || !(tmpdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR"))
        || flux_shell_setenvf (shell, 1, "PALS_SPOOL_DIR", "%s", tmpdir) < 0
        || flux_shell_setenvf (shell,
                               1,
                               "PALS_APINFO",
                               "%s",
                               apinfo_path) < 0) {
        return -1;
    }
    return 0;
}

/*
 * Create the LibPALS apinfo file in the job's tempdir and set
 * the LibPALS environment.
 */
static int libpals_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    const char *tmpdir;
    char apinfo_path[1024];
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!(tmpdir = flux_shell_getenv (shell, "FLUX_JOB_TMPDIR"))
        || snprintf (apinfo_path,
                     sizeof (apinfo_path),
                     "%s/%s",
                     tmpdir,
                     "libpals_apinfo")
               >= sizeof (apinfo_path)
        || create_apinfo (apinfo_path, shell) < 0
        || set_environment (shell, apinfo_path) < 0) {
        return -1;
    }
    return 0;
}

/*
 * Set the 'PALS_RANKID' environment variable to the value of 'FLUX_TASK_RANK'
 */
static int libpals_task_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    flux_cmd_t *cmd;
    int task_rank;

    if (!shell || !(task = flux_shell_current_task (shell))
        || !(cmd = flux_shell_task_cmd (task))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &task_rank) < 0
        || flux_cmd_setenvf (cmd, 1, "PALS_RANKID", "%d", task_rank) < 0) {
        return -1;
    }
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_set_name (p, "libpals") < 0
        || flux_plugin_add_handler (p, "shell.init", libpals_init, NULL) < 0
        || flux_plugin_add_handler (p,
                                    "task.init",
                                    libpals_task_init,
                                    NULL) < 0) {
        return -1;
    }
    return 0;
}
