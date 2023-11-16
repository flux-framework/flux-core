/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_DISCONNECT_H
#define _FLUX_CORE_DISCONNECT_H

#include "message.h"

#ifdef __cplusplus
extern "C" {
#endif

struct flux_msg_match {
    uint32_t matchtag;
    const char *route_first;
    struct flux_msg_cred cred;
};

/* Initialize a flux_msg_match struct from `msg`.
 */
int flux_msg_match_init (struct flux_msg_match *match,
                         const flux_msg_t *msg);

/* Return true if disconnect request msg1 came from same sender as
 * msg2 and has appropriate authorization
 */
bool flux_disconnect_match (const flux_msg_t *msg1, const flux_msg_t *msg2);

/* Like the above but with reusable flux_msg_match argument.
 */
bool flux_disconnect_match_ex (struct flux_msg_match *match,
                               const flux_msg_t *msg);

/* Remove all messages in 'l' with the same sender as 'msg'.
 * Return 0 or the number of messages removed.
 */
int flux_msglist_disconnect (struct flux_msglist *l, const flux_msg_t *msg);

/* Return true if cancel request msg1 came from same sender as msg2,
 * has appropriate authorization, and references the matchtag in
 * msg2 */
bool flux_cancel_match (const flux_msg_t *msg1, const flux_msg_t *msg2);

/* Like the above bu with reusable flux_msg_match argument.
 */
bool flux_cancel_match_ex (struct flux_msg_match *match,
                           const flux_msg_t *msg);

/* Respond to and remove the first message in 'l' that matches 'msg'.
 * The sender must match 'msg', and the matchtag must match the one in
 * the cancel request payload.
 */
int flux_msglist_cancel (flux_t *h,
                         struct flux_msglist *l,
                         const flux_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_DISCONNECT_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
