/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Simple pty/terminal client/server over flux messages */

#ifndef FLUX_PTY_H
#define FLUX_PTY_H

#include <flux/core.h>

typedef void (*pty_log_f) (void *arg,
                           const char *file,
                           int line,
                           const char *func,
                           const char *subsys,
                           int level,
                           const char *fmt,
                           va_list args);


struct flux_pty;
struct flux_pty_client;

typedef void (*pty_monitor_f) (struct flux_pty *pty,
                               void *data,
                               int len);

/* server:
 */

/*  Open a new server-side pty handle.
 */
struct flux_pty *flux_pty_open (void);

/*  Close pty server - `status` may be set to the wait status of a
 *   terminating child process of the pty. This status will be forwarded
 *   to any currently connected clients.
 */
void flux_pty_close (struct flux_pty *pty, int status);

/*  Send signal `sig` to the foreground process group of `pty`.
 */
int  flux_pty_kill (struct flux_pty *pty, int sig);

/*  Set internal logger for this pty instance.
 */
void flux_pty_set_log (struct flux_pty *pty,
                       pty_log_f log,
                       void *log_data);

/*  Set a callback to receive data events locally.
 *  (Usefully if we want to locally monitor the pty for logging, etc.)
 */
void flux_pty_monitor (struct flux_pty *pty, pty_monitor_f fn);

int flux_pty_leader_fd (struct flux_pty *pty);

/*  Return the follower name for this pty
 */
const char *flux_pty_name (struct flux_pty *pty);

/*  Attach the current process to follower end of pty
 *  (e.g. called from pre_exec hook of a flux_subprocess_t)
 */
int flux_pty_attach (struct flux_pty *pty);

int flux_pty_set_flux (struct flux_pty *pty, flux_t *h);

/*  Return the current number of connected clients for pty
 */
int flux_pty_client_count (struct flux_pty *pty);

/*  Send a request msg to the pty server pty. Used in a msg_handler
 *   for the pty topic string for this pty service.
 */
int flux_pty_sendmsg (struct flux_pty *pty, const flux_msg_t *msg);

/*  Add new client that recieves no data, only waits for pty to exit
 */
int flux_pty_add_exit_watcher (struct flux_pty *pty, const flux_msg_t *msg);

/*  Disconnect any client mathing sender
 */
int flux_pty_disconnect_client (struct flux_pty *pty, const char *sender);


/* client:
 */

enum {
    FLUX_PTY_CLIENT_ATTACH_SYNC =      1,  /* Synchronous attach            */
    FLUX_PTY_CLIENT_CLEAR_SCREEN =     2,  /* Clear screen on attach        */
    FLUX_PTY_CLIENT_NOTIFY_ON_ATTACH = 4,  /* Informational msg on attach   */
    FLUX_PTY_CLIENT_NOTIFY_ON_DETACH = 8,  /* Informational msg on attach   */
    FLUX_PTY_CLIENT_NORAW =           16,  /* Do not set raw mode on attach */
    FLUX_PTY_CLIENT_STDIN_PIPE =      32,  /* Pipe stdin to remote on attach*/
};

/*  Create a new pty client
 */
struct flux_pty_client *flux_pty_client_create (void);

void flux_pty_client_destroy (struct flux_pty_client *c);

int flux_pty_client_set_flags (struct flux_pty_client *c,
                               int flags);
int flux_pty_client_get_flags (struct flux_pty_client *c);

/*  Set internal pty client logging function
 */
void flux_pty_client_set_log (struct flux_pty_client *c,
                              pty_log_f log,
                              void *log_data);


/*  Attach pty client to server at rank,service endpoint. The current tty
 *   will be put into "raw" mode. Terminal will be restored when process
 *   exits, or flux_pty_client_restore_terminal() is called.
 *
 *  Will block until first attach response if FLUX_PTY_CLIENT_ATTACH_SYNC
 *   was set in flags.
 *
 *  Screen is cleared on attach if FLUX_PTY_CLIENT_CLEAR_SCREEN is set.
 */
int flux_pty_client_attach (struct flux_pty_client *c,
                            flux_t *h,
                            int rank,
                            const char *service);

/*  Write data out-of-band to remote pty.
 */
flux_future_t *flux_pty_client_write (struct flux_pty_client *c,
                                      const void *buf,
                                      ssize_t len);

/*  Send request to pty server to detach the current client.
 *  Client will call exit callback when detach is complete.
 */
int flux_pty_client_detach (struct flux_pty_client *c);


typedef void (*flux_pty_client_exit_f) (struct flux_pty_client *c,
                                        void *arg);

/*  Call supplied function when pty client `c` "exits" i.e. detaches
 *   or remote pty exits.
 */
int flux_pty_client_notify_exit (struct flux_pty_client *c,
                                 flux_pty_client_exit_f fn,
                                 void *arg);

int flux_pty_client_exit_status (struct flux_pty_client *c, int *statusp);

void flux_pty_client_restore_terminal (void);

int flux_pty_aux_set (struct flux_pty *pty,
                      const char *key,
                      void *val,
                      flux_free_f destroy);

void * flux_pty_aux_get (struct flux_pty *pty, const char *name);

#endif /* !FLUX_PTY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
