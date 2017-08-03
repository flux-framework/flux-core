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

#ifndef HAVE_CRON_TASK_H
#define HAVE_CRON_TASK_H

#include <jansson.h>
#include <flux/core.h>

/*  cron_task_t: async task handling for cron
 */
typedef struct cron_task cron_task_t;

/*  io callback fn for cron task
 */
typedef void (*cron_task_io_f) (flux_t *h, cron_task_t *t, void *arg,
                                bool is_stderr, void *data, int datalen,
                                bool eof);

/*  task state change handler for cron task, check state with
 *   cron_task_state().
 */
typedef void (*cron_task_state_f) (flux_t *h, cron_task_t *t, void *arg);

/*  task completion handler, the only required handler for cron task,
 *   called when task and its I/O have completed.
 */
typedef void (*cron_task_complete_f) (flux_t *h, cron_task_t *t, void *arg);

/*  create a new cron task using flux handle `h`. Completion handler
 *   `cb` will be called when cron task has fully completed. All callbacks
 *   will be passed context `arg`.
 */
cron_task_t *cron_task_new (flux_t *h, cron_task_complete_f cb, void *arg);

/*  destroy cron task `t`
 */
void cron_task_destroy (cron_task_t *t);

/*  call `cb` on any io for cron task `t`
 */
void cron_task_on_io (cron_task_t *t, cron_task_io_f cb);

/*  call `cb` on any state change in cron task `t`
 */
void cron_task_on_state_change (cron_task_t *t, cron_task_state_f cb);

/*  set a timeout on execution time of task `t` for `to` seconds.
 *   if callback `cb` is set then it will be called at the timeout,
 *   if cb == NULL then the task is automatically sent SIGTERM.
 */
void cron_task_set_timeout (cron_task_t *t, double to, cron_task_state_f cb);

/*  run cron task `t` as command `cmd`, optional working directory `cwd`
 *   and optional alternate environment `env`.
 */
int cron_task_run (cron_task_t *t,
                   int rank, const char *cmd,
                   const char *cwd,
                   char *const env []);

/*
 *  Send signal `sig` to cron task t
 */
int cron_task_kill (cron_task_t *t, int sig);

/*  return string representation of the current cron task state
 */
const char * cron_task_state (cron_task_t *t);

/*  return exit status, or -1 if task not exited
 */
int cron_task_status (cron_task_t *t);

/*  return JSON representation of cron task `t`
 */
json_t *cron_task_to_json (cron_task_t *t);

#endif /* !HAVE_CRON_TASK_H */

/* vi: ts=4 sw=4 expandtab
 */

