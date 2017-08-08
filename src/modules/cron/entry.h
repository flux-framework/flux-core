/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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

#ifndef HAVE_CRON_ENTRY_H
# define HAVE_CRON_ENTRY_H

#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

typedef struct cron_ctx cron_ctx_t;
typedef struct cron_entry cron_entry_t;

/*
 *  Cron entry type-specific operations
 */
struct cron_entry_ops {
    // type creator from JSON request "arguments". Returns ptr to type object
    void *(*create) (flux_t *h, cron_entry_t *e, json_t *arg);

    // destroy type object contained in data
    void (*destroy) (void *data);

    // start the type specific watcher
    void (*start) (void *data);

    // stop the type specific watcher
    void (*stop) (void *data);

    // return data for entry type as JSON
    json_t *(*tojson) (void *data);
};

struct cron_stats {
    double     ctime;        /* entry creation time           */
    double     lastrun;      /* last time task was launched   */
    double     starttime;    /* last time entry was started   */
    uint64_t   total;        /* total number of runs          */
    uint64_t   count;        /* number of runs since start    */
    uint64_t   failcount;    /* number failed runs since start*/
    uint64_t   success;      /* number of successes           */
    uint64_t   failure;      /* number of failures            */
    uint64_t   deferred;     /* number of times deferred      */
};


struct cron_entry {
    cron_ctx_t    *     ctx;                /* cron ctx for this entry       */
    int                 destroyed;          /* Entry is defunct              */

    struct cron_stats   stats;              /* meta-stats for this entry     */

    int64_t             id;                 /* Unique sequence number        */
    int                 rank;               /* Optional rank on which to run */
    char *              name;               /* Entry name, if given          */
    char *              command;            /* Command to execute            */
    char *              cwd;                /* Change working directory      */
    json_t *            env;                /* Optional environment for cmd,
                                               (encoded as json array)       */

    int                 repeat;             /* Total number of times to run  */

    unsigned int        stopped:1;          /* This entry is inactive        */

    char *                 typename;        /* Name of this type             */
    struct cron_entry_ops  ops;             /* Type-specific operations      */
    void *                 data;            /* Entry type specific data      */

    struct cron_task *  task;               /* Currently executing task      */
    zlist_t          *  completed_tasks;    /* List of completed tasks       */
    int                 task_history_count; /* Max # of tasks in history     */
    int                 stop_on_failure;    /* Stop cron entry after failure */

    double              timeout;            /* Max secs to allow task to run */
};

/* Return type data ptr for cron_entry `e`
 */
void *cron_entry_type_data (cron_entry_t *e);

/* Schedule the task corresponding to cron entry `e` to run as soon as allowed
 */
int cron_entry_schedule_task (cron_entry_t *e);

/* Stop the current entry in the next prepare watcher.
 */
int cron_entry_stop_safe (cron_entry_t *e);

/* Retrieve current timestamp in seconds
 */
double get_timestamp (void);

#endif /* !HAVE_CRON_ENTRY_H */
