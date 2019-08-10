/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SUBPROCESS_PRIVATE_H
#define _SUBPROCESS_PRIVATE_H

#include "subprocess.h"

#define SUBPROCESS_MAGIC           0xbeefcafe

#define SUBPROCESS_SERVER_MAGIC    0xbeefbeef

#define SUBPROCESS_DEFAULT_BUFSIZE 1048576

#define CHANNEL_MAGIC    0xcafebeef

#define CHANNEL_READ  0x01
#define CHANNEL_WRITE 0x02
#define CHANNEL_FD    0x04

struct subprocess_channel {
    int magic;

    flux_subprocess_t *p;
    flux_subprocess_output_f output_f;
    char *name;
    int flags;

    /* caller info */
    bool eof_sent_to_caller;       /* eof sent to user */
    bool closed;

    /* local */
    int parent_fd;
    int child_fd;
    flux_watcher_t *buffer_write_w;
    flux_watcher_t *buffer_read_w;
    /* buffer_read_stopped_w is a "sub-in" watcher if buffer_read_w is
     * stopped.  We need to put something into the reactor so we know
     * that we still need to process it.
     */
    flux_watcher_t *buffer_read_stopped_w;
    bool buffer_read_w_started;

    /* remote */
    flux_buffer_t *write_buffer;
    flux_buffer_t *read_buffer;
    bool write_eof_sent;
    bool read_eof_received;
    flux_watcher_t *in_prep_w;
    flux_watcher_t *in_idle_w;
    flux_watcher_t *in_check_w;
    flux_watcher_t *out_prep_w;
    flux_watcher_t *out_idle_w;
    flux_watcher_t *out_check_w;

    /* misc */
    bool line_buffered;         /* for buffer_read_w / read_buffer */
};

struct flux_subprocess {
    int magic;

    flux_t *h;
    flux_reactor_t *reactor;
    uint32_t rank;
    int flags;
    bool local;                     /* This is a local process, not remote. */

    int refcount;
    pid_t pid;
    bool pid_set;

    flux_subprocess_ops_t ops;      /* Callbacks registered for this proc */

    flux_cmd_t *cmd;                /* readonly/o copy of the command     */

    struct aux_item *aux;           /* auxillary data                     */

    zhash_t *channels;              /* hash index by name to channel info */
    int channels_eof_expected;      /* number of eofs to expect */
    int channels_eof_sent;          /* counter to avoid loop checks */

    int status;      /* Raw status from waitpid(2), valid if exited       */
    int exec_failed_errno;  /* Holds errno from exec(2) if exec() failed  */

    flux_subprocess_state_t state;
    flux_subprocess_state_t state_reported; /* for on_state_change */
    flux_watcher_t *state_prep_w;
    flux_watcher_t *state_idle_w;
    flux_watcher_t *state_check_w;

    bool completed;             /* process has exited and i/o is complete */
    flux_watcher_t *completed_prep_w;
    flux_watcher_t *completed_idle_w;
    flux_watcher_t *completed_check_w;

    /* local */

    /* fds[0] is parent/user, fds[1] is child */
    int sync_fds[2];                /* socketpair for fork/exec sync      */
    bool in_hook;                   /* if presently in a hook */
    flux_watcher_t *child_w;
    flux_subprocess_hooks_t hooks;

    /* remote */

    flux_future_t *f;           /* primary future reactor */
    bool remote_completed;      /* if remote has completed */
    int failed_errno;           /* Holds errno if FAILED state reached */
    int signal_pending;         /* signal sent while starting */
};

struct flux_subprocess_server {
    int magic;
    flux_t *h;
    flux_reactor_t *r;
    char *local_uri;
    uint32_t rank;
    zhash_t *subprocesses;
    flux_msg_handler_t **handlers;

    /* for teardown / termination */
    flux_watcher_t *terminate_timer_w;
    flux_watcher_t *terminate_prep_w;
    flux_watcher_t *terminate_idle_w;
    flux_watcher_t *terminate_check_w;
};

void subprocess_check_completed (flux_subprocess_t *p);

void state_change_start (flux_subprocess_t *p);

void channel_destroy (void *arg);

struct subprocess_channel *channel_create (flux_subprocess_t *p,
                                           flux_subprocess_output_f output_f,
                                           const char *name,
                                           int flags);

#endif /* !_SUBPROCESS_PRIVATE_H */
