/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_ROUTE_H
#define _FLUX_CORE_MESSAGE_ROUTE_H

struct route_id {
    struct list_node route_id_node;
    char id[0];                 /* variable length id stored at end of struct */
};

int msg_route_push (flux_msg_t *msg,
                    const char *id,
                    unsigned int id_len);

int msg_route_append (flux_msg_t *msg,
                      const char *id,
                      unsigned int id_len);

void msg_route_clear (flux_msg_t *msg);

int msg_route_delete_last (flux_msg_t *msg);

#endif /* !_FLUX_CORE_MESSAGE_ROUTE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

