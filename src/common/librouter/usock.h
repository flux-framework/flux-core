/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_USOCK_H
#define _ROUTER_USOCK_H

#include <sys/types.h>
#include <flux/core.h>

#include "auth.h"

struct usock_conn;
struct usock_client;

struct usock_retry_params {
    int max_retry;      // maximum connect retry count
    double min_delay;   // initial retry delay (s)
    double max_delay;   // retry delay cap (s)
};

#define USOCK_RETRY_DEFAULT (struct usock_retry_params){ \
    .max_retry = 5, \
    .min_delay = 0.016, \
    .max_delay = 2, \
}
#define USOCK_RETRY_NONE (struct usock_retry_params){ \
    .max_retry = 0, \
    .min_delay = 0, \
    .max_delay = 0, \
}

typedef void (*usock_acceptor_f)(struct usock_conn *conn, void *arg);

typedef void (*usock_conn_close_f)(struct usock_conn *conn,
                                   void *arg);
typedef void (*usock_conn_error_f)(struct usock_conn *conn,
                                   int errnum,
                                   void *arg);
typedef void (*usock_conn_recv_f)(struct usock_conn *conn,
                                  flux_msg_t *msg,
                                  void *arg);

/* Server
 */

struct usock_server *usock_server_create (flux_reactor_t *r,
                                          const char *sockpath,
                                          int mode);
void usock_server_destroy (struct usock_server *server);

void usock_server_set_acceptor (struct usock_server *server,
                                usock_acceptor_f cb,
                                void *arg);

// accessor for start/stop/ref/unref
flux_watcher_t *usock_server_listen_watcher (struct usock_server *server);

/* Server connection for one client
 */

int usock_conn_send (struct usock_conn *conn, const flux_msg_t *msg);

const struct flux_msg_cred *usock_conn_get_cred (struct usock_conn *conn);

const char *usock_conn_get_uuid (struct usock_conn *conn);

void usock_conn_set_close_cb (struct usock_conn *conn,
                              usock_conn_close_f cb,
                              void *arg);

void usock_conn_set_error_cb (struct usock_conn *conn,
                              usock_conn_error_f cb,
                              void *arg);

void usock_conn_set_recv_cb (struct usock_conn *conn,
                             usock_conn_recv_f cb,
                             void *arg);

void usock_conn_accept (struct usock_conn *conn,
                        const struct flux_msg_cred *cred);
void usock_conn_reject (struct usock_conn *conn, int errnum);

void *usock_conn_aux_get (struct usock_conn *conn, const char *name);

int usock_conn_aux_set (struct usock_conn *conn,
                        const char *name,
                        void *aux,
                        flux_free_f destroy);

void usock_conn_destroy (struct usock_conn *conn);

struct usock_conn *usock_conn_create (flux_reactor_t *r, int infd, int outfd);

/* Client
 */

int usock_client_pollevents (struct usock_client *client);
int usock_client_pollfd (struct usock_client *client);

int usock_client_send (struct usock_client *client,
                       const flux_msg_t *msg,
                       int flags);
flux_msg_t *usock_client_recv (struct usock_client *client, int flags);

int usock_client_connect (const char *sockpath,
                          struct usock_retry_params retry);

struct usock_client *usock_client_create (int fd);
void usock_client_destroy (struct usock_client *client);

#endif /* !_ROUTER_USOCK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
