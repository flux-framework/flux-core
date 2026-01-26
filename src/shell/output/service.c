/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell std output write service implementation
 *
 * When output is going to the KVS or a single output file, the
 * leader shell implements this "shell-<id>.write" service to which
 * client shell ranks send output (see output/client.c).
 *
 * Clients may send an RFC 24 encoded data event, and "eof" event
 * to indicate no more output is forthcoming, or a "log" event for
 * propagation of log messages from other job shells.
 *
 * Local task and logging output is not routed through this service
 * code.
 *
 * The output service includes a reference for each remote shell. If
 * there are no remote shells then the service is not started. When
 * the service is in use, an 'output.write' completion reference is
 * taken on the job shell to ensure the shell and this service remain
 * active. Once all remote shells have sent "eof", then the reference
 * is dropped.
 *
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>

#define FLUX_SHELL_PLUGIN_NAME "output.service"

#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "ccan/str/str.h"

#include "output/output.h"

struct output_service {
    struct shell_output *out;
    int refcount;
    struct idset *active_shells;
};

void output_service_destroy (struct output_service *service)
{
    if (service) {
        int saved_errno = errno;
        idset_destroy (service->active_shells);
        free (service);
        errno = saved_errno;
    }
}

static void output_service_decref (struct output_service *service)
{
    if (--service->refcount == 0) {
        if (flux_shell_remove_completion_ref (service->out->shell,
                                              "output.service") < 0)
            shell_log_errno ("flux_shell_remove_completion_ref");

        /* Remove output service reference from shell output
         */
        shell_output_decref (service->out);
    }
}

static void output_service_decref_shell_rank (struct output_service *service,
                                              int shell_rank)
{
    if (idset_test (service->active_shells, shell_rank)
        && idset_clear (service->active_shells, shell_rank) == 0)
        output_service_decref (service);
}

static int output_service_write (struct output_service *service,
                                 const char *type,
                                 int shell_rank,
                                 json_t *o)
{
    if (streq (type, "eof")) {
        output_service_decref_shell_rank (service, shell_rank);
        return 0;
    }
    return shell_output_write_entry (service->out, type, o);
}

static void output_service_write_cb (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg)
{
    struct output_service *service = arg;
    int shell_rank = -1;
    json_t *o;
    const char *type;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i s:o}",
                             "name", &type,
                             "shell_rank", &shell_rank,
                             "context", &o) < 0
        || output_service_write (service, type, shell_rank, o) < 0)
        shell_log_errno ("error recording write data for rank %d", shell_rank);
}

static void output_service_write_getcredit_cb (flux_t *h,
                                               flux_msg_handler_t *mh,
                                               const flux_msg_t *msg,
                                               void *arg)
{
    int credits;

    if (flux_request_unpack (msg, NULL, "{s:i}", "credits", &credits) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i}", "credits", credits) < 0)
        shell_log_errno ("error responding to write-getcredit");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        shell_log_errno ("error responding to write-getcredit");
}

static int shell_lost (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct output_service *service = data;
    int shell_rank;

    /*  A shell has been lost. We need to decref the output refcount by 1
     *  since we'll never hear from that shell to avoid rank 0 shell from
     *  hanging.
     */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:i}",
                                "shell_rank", &shell_rank) < 0)
        return shell_log_errno ("shell.lost: unpack of shell_rank failed");
    output_service_decref_shell_rank (service, shell_rank);
    shell_debug ("lost shell rank %d", shell_rank);
    return 0;
}

struct output_service *output_service_create (struct shell_output *out,
                                              flux_plugin_t *p,
                                              int size)
{
    struct output_service *service;

    if (!(service = calloc (1, sizeof (*service))))
        return NULL;

    /* Add a reference for each remote shell */
    service->refcount = size - 1;

    /* Nothing to do if refcount is zero. Just return empty service object
     */
    if (service->refcount == 0)
        return service;

    service->out = out;

    if (!(service->active_shells = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (service->active_shells, 0, size - 1) < 0
        || flux_plugin_add_handler (p, "shell.lost", shell_lost, service) < 0
        || flux_shell_add_completion_ref (out->shell, "output.service") < 0
        || flux_shell_service_register (out->shell,
                                        "write",
                                        output_service_write_cb,
                                        service) < 0
        || flux_shell_service_register (out->shell,
                                        "write-getcredit",
                                        output_service_write_getcredit_cb,
                                        service) < 0)
        goto error;

    /* Output service takes a reference on shell output
     */
    shell_output_incref (out);
    return service;
error:
    output_service_destroy (service);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
