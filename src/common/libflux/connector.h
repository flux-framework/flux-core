/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_CONNECTOR_H
#define _FLUX_CORE_CONNECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 ** Only handle implementation stuff below.
 ** Flux_t handle users should not use these interfaces.
 */
typedef flux_t *(connector_init_f) (const char *uri, int flags);

struct flux_handle_ops {
    int (*setopt) (void *impl, const char *option, const void *val, size_t len);
    int (*getopt) (void *impl, const char *option, void *val, size_t len);
    int (*pollfd) (void *impl);
    int (*pollevents) (void *impl);
    int (*send) (void *impl, const flux_msg_t *msg, int flags);
    flux_msg_t *(*recv) (void *impl, int flags);

    int (*event_subscribe) (void *impl, const char *topic);
    int (*event_unsubscribe) (void *impl, const char *topic);

    void (*impl_destroy) (void *impl);
};

flux_t *flux_handle_create (void *impl, const struct flux_handle_ops *ops, int flags);
void flux_handle_destroy (flux_t *hp);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_CONNECTOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
