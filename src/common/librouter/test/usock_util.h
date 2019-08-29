/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

struct cli;

typedef void (*cli_recv_f)(struct cli *cli, const flux_msg_t *msg, void *arg);

struct cli;

int cli_send (struct cli *cli, const flux_msg_t *msg);

void cli_destroy (struct cli *cli);

struct cli *cli_create (flux_reactor_t *r,
                        int fd,
                        cli_recv_f recv_cb,
                        void *arg);

/*
 * vi: ts=4 sw=4 expandtab
 */
