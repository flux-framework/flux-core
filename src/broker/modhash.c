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
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "ccan/str/str.h"

#include "module.h"
#include "modhash.h"

struct modhash {
    zhash_t *zh_byuuid;
};

int modhash_response_sendmsg_new (modhash_t *mh, flux_msg_t **msg)
{
    const char *uuid;
    module_t *p;

    if (!*msg)
        return 0;
    if (!(uuid = flux_msg_route_last (*msg))) {
        errno = EPROTO;
        return -1;
    }
    if (!(p = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOSYS;
        return -1;
    }
    return module_sendmsg_new (p, msg);
}

void modhash_add (modhash_t *mh, module_t *p)
{
    int rc;

    rc = zhash_insert (mh->zh_byuuid, module_get_uuid (p), p);
    assert (rc == 0); /* uuids are by definition unique */
    zhash_freefn (mh->zh_byuuid,
                  module_get_uuid (p),
                  (zhash_free_fn *)module_destroy);
}

void modhash_remove (modhash_t *mh, module_t *p)
{
    zhash_delete (mh->zh_byuuid, module_get_uuid (p));
}

modhash_t *modhash_create (void)
{
    modhash_t *mh = calloc (1, sizeof (*mh));
    if (!mh)
        return NULL;
    if (!(mh->zh_byuuid = zhash_new ())) {
        errno = ENOMEM;
        modhash_destroy (mh);
        return NULL;
    }
    return mh;
}

int modhash_destroy (modhash_t *mh)
{
    int saved_errno = errno;
    const char *uuid;
    module_t *p;
    int count = 0;

    if (mh) {
        if (mh->zh_byuuid) {
            FOREACH_ZHASH (mh->zh_byuuid, uuid, p) {
                log_msg ("broker module '%s' was not properly shut down",
                         module_get_name (p));
                flux_error_t error;
                if (module_cancel (p, &error) < 0)
                    log_msg ("%s: %s", module_get_name (p), error.text);
                count++;
            }
            zhash_destroy (&mh->zh_byuuid);
        }
        free (mh);
    }
    errno = saved_errno;
    return count;
}

static json_t *modhash_entry_tojson (module_t *p,
                                     double now,
                                     struct service_switch *sw)
{
    json_t *svcs;
    json_t *entry = NULL;

    if (!(svcs  = service_list_byuuid (sw, module_get_uuid (p))))
        return NULL;
    entry = json_pack ("{s:s s:s s:i s:i s:O s:i s:i}",
                       "name", module_get_name (p),
                       "path", module_get_path (p),
                       "idle", (int)(now - module_get_lastseen (p)),
                       "status", module_get_status (p),
                       "services", svcs,
                       "sendqueue", module_get_send_queue_count (p),
                       "recvqueue", module_get_recv_queue_count (p));
    json_decref (svcs);
    return entry;
}

json_t *modhash_get_modlist (modhash_t *mh,
                             double now,
                             struct service_switch *sw)
{
    json_t *mods = NULL;
    module_t *p;

    if (!(mods = json_array()))
        goto nomem;
    p = zhash_first (mh->zh_byuuid);
    while (p) {
        json_t *entry;

        if (!(entry = modhash_entry_tojson (p, now, sw))
            || json_array_append_new (mods, entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        p = zhash_next (mh->zh_byuuid);
    }
    return mods;
nomem:
    json_decref (mods);
    errno = ENOMEM;
    return NULL;
}

module_t *modhash_lookup (modhash_t *mh, const char *uuid)
{
    module_t *m;

    if (!(m = zhash_lookup (mh->zh_byuuid, uuid))) {
        errno = ENOENT;
        return NULL;
    }
    return m;
}

module_t *modhash_lookup_byname (modhash_t *mh, const char *name)
{
    zlist_t *uuids;
    char *uuid;
    module_t *result = NULL;

    if (!(uuids = zhash_keys (mh->zh_byuuid))) {
        errno = ENOMEM;
        return NULL;
    }
    uuid = zlist_first (uuids);
    while (uuid) {
        module_t *p = zhash_lookup (mh->zh_byuuid, uuid);
        if (p) {
            if (streq (module_get_name (p), name)
                || streq (module_get_path (p), name)) {
                result = p;
                break;
            }
        }
        uuid = zlist_next (uuids);
    }
    zlist_destroy (&uuids);
    return result;
}

int modhash_event_mcast (modhash_t *mh, const flux_msg_t *msg)
{
    module_t *p;

    p = zhash_first (mh->zh_byuuid);
    while (p) {
        if (module_event_cast (p, msg) < 0)
            return -1;
        p = zhash_next (mh->zh_byuuid);
    }
    return 0;
}

module_t *modhash_first (modhash_t *mh)
{
    return zhash_first (mh->zh_byuuid);
}

module_t *modhash_next (modhash_t *mh)
{
    return zhash_next (mh->zh_byuuid);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
