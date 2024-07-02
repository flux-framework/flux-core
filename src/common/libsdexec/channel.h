/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_CHANNEL_H
#define _LIBSDEXEC_CHANNEL_H

#include <jansson.h>
#include <flux/core.h>

struct channel;

enum {
    CHANNEL_LINEBUF = 1,
};

typedef void (*channel_output_f)(struct channel *ch, json_t *io, void *arg);
typedef void (*channel_error_f)(struct channel *ch,
                                flux_error_t *error,
                                void *arg);

/* Open a channel for output from the systemd unit.  When the unit has written
 * some data, an internal fd watcher buffers it, then invokes output_cb.
 * If there is a read error, the error_cb is also called for logging, then
 * output_cb is called with EOF.
 *
 * Notes:
 * - internal watcher is not started until sdexec_channel_start_output()
 *   is called
 * - data is line buffered if 'flags' includes CHANNEL_LINEBUF
 * - a single callback may not represent all the data available at that moment
 */
struct channel *sdexec_channel_create_output (flux_t *h,
                                              const char *name,
                                              size_t bufsize,
                                              int flags,
                                              channel_output_f output_cb,
                                              channel_error_f error_cb,
                                              void *arg);

/* Open a channel for input to the systemd unit.
 * The channel may be written to using sdexec_channel_write().
 */
struct channel *sdexec_channel_create_input (flux_t *h, const char *name);

/* Write to channel created with sdexec_channel_create_input ().
 * The ioencoded object's rank and stream name are ignored.
 * This is potentially a blocking operation if the socketpair cannot
 * accept all the data.
 */
int sdexec_channel_write (struct channel *ch, json_t *io);

/* Get fd for systemd end of socketpair.  Returns -1 if unset or ch==NULL.
 */
int sdexec_channel_get_fd (struct channel *ch);

/* Get name of channel.
 */
const char *sdexec_channel_get_name (struct channel *ch);

/* Close fd on systemd end of socketpair.
 * Call this after systemd has received the fd and duped it - in the response
 * handler for StartTransientUnit should be correct.
 */
void sdexec_channel_close_fd (struct channel *ch);

/* Start watching for channel output.
 */
void sdexec_channel_start_output (struct channel *ch);

void sdexec_channel_destroy (struct channel *ch);

json_t *sdexec_channel_get_stats (struct channel *ch);

#endif /* !_LIBSDEXEC_CHANNEL_H */

// vi:ts=4 sw=4 expandtab
