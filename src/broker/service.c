/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

#include "service.h"

struct service {
    service_send_f cb;
    void *cb_arg;
    char *uuid;
};

struct service_switch {
    zhash_t *services;
};

struct service_switch *service_switch_create (void)
{
    struct service_switch *sw = calloc (1, sizeof *sw);
    if (!sw)
        goto error;
    if (!(sw->services = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    return sw;
error:
    service_switch_destroy (sw);
    return NULL;
}

void service_switch_destroy (struct service_switch *sw)
{
    if (sw) {
        zhash_destroy (&sw->services);
        free (sw);
    }
}

static void service_destroy (struct service *svc)
{
    if (svc) {
        free (svc->uuid);
        free (svc);
    }
}

static struct service *service_create (const char *uuid)
{
    struct service *svc;

    if (!(svc = calloc (1, sizeof (*svc))))
        goto error;
    if (uuid) {
        if (!(svc->uuid = strdup (uuid)))
            goto error;
    }
    return svc;
error:
    service_destroy (svc);
    return NULL;
}

void service_remove (struct service_switch *sw, const char *name)
{
    zhash_delete (sw->services, name);
}

const char *service_get_uuid (struct service_switch *sw, const char *name)
{
    struct service *svc = zhash_lookup (sw->services, name);
    if (!svc)
        return (NULL);
    return (svc->uuid);
}

json_t *service_list_byuuid (struct service_switch *sw, const char *uuid)
{
    json_t *svcs;
    struct service *svc;

    if (!(svcs = json_array ()))
        return NULL;
    svc = zhash_first (sw->services);
    while (svc) {
        if (uuid && svc->uuid && !strcmp (uuid, svc->uuid)) {
            json_t *name = json_string (zhash_cursor (sw->services));
            if (!name)
                goto error;
            if (json_array_append_new (svcs, name) < 0) {
                json_decref (name);
                goto error;
            }
        }
        svc = zhash_next (sw->services);
    }
    return svcs;
error:
    json_decref (svcs);
    return NULL;
}

/* Delete all services registered by 'uuid'.
 */
void service_remove_byuuid (struct service_switch *sw, const char *uuid)
{
    struct service *svc;
    zlist_t *trash = NULL;
    const char *key;

    svc = zhash_first (sw->services);
    while (svc != NULL) {
        if (svc->uuid && !strcmp (svc->uuid, uuid)) {
            if (!trash)
                trash = zlist_new ();
            if (!trash)
                break;
            if (zlist_push (trash, (char *)zhash_cursor (sw->services)) < 0)
                break;
        }
        svc = zhash_next (sw->services);
    }
    if (trash) {
        while ((key = zlist_pop (trash)))
            zhash_delete (sw->services, key);
        zlist_destroy (&trash);
    }
}

int service_add (struct service_switch *sh,
                 const char *name,
                 const char *uuid,
                 service_send_f cb,
                 void *arg)
{
    struct service *svc = NULL;

    if (strchr (name, '.')) {
        errno = EINVAL;
        goto error;
    }
    if (zhash_lookup (sh->services, name)) {
        errno = EEXIST;
        goto error;
    }
    svc = service_create (uuid);
    svc->cb = cb;
    svc->cb_arg = arg;
    if (zhash_insert (sh->services, name, svc) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zhash_freefn (sh->services, name, (zhash_free_fn *)service_destroy);
    return 0;
error:
    service_destroy (svc);
    return -1;
}

/* Look up a service named 'topic', truncated to 'length' chars.
 * Avoid an extra malloc here if the substring is short.
 */
static struct service *service_lookup_subtopic (struct service_switch *sw,
                                                const char *topic,
                                                int length)
{
    char buf[16];
    char *cpy = NULL;
    char *service;
    struct service *svc = NULL;

    if (length < sizeof (buf))
        service = buf;
    else {
        if (!(cpy = malloc (length + 1)))
            goto done;
        service = cpy;
    }
    memcpy (service, topic, length);
    service[length] = '\0';

    if (!(svc = zhash_lookup (sw->services, service))) {
        errno = ENOSYS;
        goto done;
    }
done:
    free (cpy);
    return svc;
}

/* Look up a service by first "word" of topic string.
 * If found, call the service's callback and return its return value.
 * If not found, return -1 with errno set (usually ENOSYS).
 */
int service_send (struct service_switch *sw, const flux_msg_t *msg)
{
    const char *topic, *p;
    int length;
    struct service *svc;

    if (flux_msg_get_topic (msg, &topic) < 0)
        return -1;
    if ((p = strchr (topic, '.')))
        length = p - topic;
    else
        length = strlen (topic);
    if (!(svc = service_lookup_subtopic (sw, topic, length)))
        return -1;

    return svc->cb (msg, svc->cb_arg);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
