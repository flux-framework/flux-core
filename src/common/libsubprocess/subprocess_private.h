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

#include <flux/idset.h>
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "subprocess.h"
#include "fbuf_watcher.h"

#define SUBPROCESS_DEFAULT_BUFSIZE 4194304

#define CHANNEL_READ  0x01
#define CHANNEL_WRITE 0x02
#define CHANNEL_FD    0x04

struct subprocess_channel {
    flux_subprocess_t *p;
    flux_subprocess_output_f output_cb;
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
    struct fbuf *write_buffer;
    struct fbuf *read_buffer;
    bool write_eof_sent;
    bool read_eof_received;
    flux_watcher_t *in_prep_w;
    flux_watcher_t *in_idle_w;
    flux_watcher_t *in_check_w;
    flux_watcher_t *out_prep_w;
    flux_watcher_t *out_idle_w;
    flux_watcher_t *out_check_w;
    /* for FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF */
    const char *unbuf_data;
    int unbuf_len;

    /* misc */
    bool line_buffered;         /* for buffer_read_w / read_buffer */
};

struct flux_subprocess {
    flux_t *h;
    flux_reactor_t *reactor;
    uint32_t rank;
    int flags;
    bool local;                     /* This is a local process, not remote. */

    subprocess_log_f llog;
    void *llog_data;

    int refcount;
    pid_t pid;
    bool pid_set;

    flux_subprocess_ops_t ops;      /* Callbacks registered for this proc */

    flux_cmd_t *cmd;                /* readonly/o copy of the command     */

    struct aux_item *aux;           /* auxiliary data                     */

    zhash_t *channels;              /* hash index by name to channel info */
    int channels_eof_expected;      /* number of eofs to expect */
    int channels_eof_sent;          /* counter to avoid loop checks */

    int status;      /* Raw status from waitpid(2), valid if exited       */

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

    char *service_name;
    flux_future_t *f;           /* primary future reactor */
    bool remote_completed;      /* if remote has completed */
    int failed_errno;           /* Holds errno if FAILED state reached */
    flux_error_t failed_error;  /* Holds detailed message for failed_errno */
    int signal_pending;         /* signal sent while starting */
};

void subprocess_check_completed (flux_subprocess_t *p);

void state_change_start (flux_subprocess_t *p);

void channel_destroy (void *arg);

struct subprocess_channel *channel_create (flux_subprocess_t *p,
                                           flux_subprocess_output_f output_cb,
                                           const char *name,
                                           int flags);

struct idset * subprocess_childfds (flux_subprocess_t *p);

void subprocess_incref (flux_subprocess_t *p);
void subprocess_decref (flux_subprocess_t *p);

void subprocess_standard_output (flux_subprocess_t *p, const char *name);

#endif /* !_SUBPROCESS_PRIVATE_H */

// vi: ts=4 sw=4 expandtab
