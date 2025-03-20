/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libutil/jpath.h"

#include "ccan/str/str.h"

flux_msg_t *cred_msg_pack (const char *topic,
                           struct flux_msg_cred cred,
                           const char *fmt,
                           ...)
{
    flux_msg_t *newmsg = NULL;
    flux_msg_t *rv = NULL;
    int save_errno;
    va_list ap;

    va_start (ap, fmt);

    if (!(newmsg = flux_request_encode (topic, NULL)))
        goto error;
    if (flux_msg_set_cred (newmsg, cred) < 0)
        goto error;
    if (flux_msg_vpack (newmsg, fmt, ap) < 0)
        goto error;
    rv = newmsg;
error:
    save_errno = errno;
    if (!rv)
        flux_msg_destroy (newmsg);
    va_end (ap);
    errno = save_errno;
    return rv;
}

bool get_next_eventlog_entry (const char **pp,
                              const char **tok,
                              size_t *toklen)
{
    char *term;

    if (!(term = strchr (*pp, '\n')))
        return false;
    *tok = *pp;
    *toklen = term - *pp + 1;
    *pp = term + 1;
    return true;
}

int parse_eventlog_entry (flux_t *h,
                          const char *tok,
                          size_t toklen,
                          json_t **entry,
                          const char **name,
                          json_t **context)
{
    char *str = NULL;
    json_t *o = NULL;
    int saved_errno, rc = -1;

    if (!(str = strndup (tok, toklen))) {
        flux_log_error (h, "%s: strndup", __FUNCTION__);
        goto error;
    }

    if (!(o = eventlog_entry_decode (str))) {
        flux_log_error (h, "%s: eventlog_entry_decode", __FUNCTION__);
        goto error;
    }

    if (eventlog_entry_parse (o, NULL, name, context) < 0) {
        flux_log_error (h, "%s: eventlog_entry_parse", __FUNCTION__);
        goto error;
    }

    (*entry) = o;
    free (str);
    return 0;

error:
    saved_errno = errno;
    free (str);
    json_decref (o);
    errno = saved_errno;
    return rc;
}

void apply_updates_R (flux_t *h,
                      flux_jobid_t id,
                      const char *key,
                      json_t *R,
                      json_t *context)
{
    const char *ckey;
    json_t *value;

    json_object_foreach (context, ckey, value) {
        /* RFC 21 resource-update event only allows update
         * to:
         * - expiration
         */
        if (streq (ckey, "expiration"))
            if (jpath_set (R,
                           "execution.expiration",
                           value) < 0) {
                flux_log (h,
                          LOG_INFO,
                          "%s: failed to update job %s %s",
                          __FUNCTION__,
                          idf58 (id),
                          key);
                }
    }
}

void apply_updates_jobspec (flux_t *h,
                            flux_jobid_t id,
                            const char *key,
                            json_t *jobspec,
                            json_t *context)
{
    const char *ckey;
    json_t *value;

    json_object_foreach (context, ckey, value) {
        if (jpath_set (jobspec,
                       ckey,
                       value) < 0) {
            flux_log (h,
                      LOG_INFO,
                      "%s: failed to update job %s %s",
                      __FUNCTION__,
                      idf58 (id),
                      key);
        }
    }
}

char *create_matchtag_key (flux_t *h, const flux_msg_t *msg)
{
    char *matchtag_key;
    uint32_t matchtag;
    const char *uuid;

    if (flux_msg_get_matchtag (msg, &matchtag) < 0) {
        flux_log_error (h, "failed to get matchtag");
        return NULL;
    }
    if (!(uuid = flux_msg_route_first (msg))) {
        flux_log_error (h, "failed to get uuid");
        return NULL;
    }
    if (asprintf (&matchtag_key, "%s:%u", uuid, matchtag) < 0)
        return NULL;
    return matchtag_key;
}

int get_matchtag_key (flux_t *h,
                      const flux_msg_t *msg,
                      char *buf,
                      size_t bufsize)
{
    uint32_t matchtag;
    const char *uuid;
    if (flux_msg_unpack (msg, "{s:i}", "matchtag", &matchtag) < 0) {
        flux_log_error (h, "failed to get matchtag");
        return -1;
    }
    if (!(uuid = flux_msg_route_first (msg))) {
        flux_log_error (h, "failed to get uuid");
        return -1;
    }
    if (snprintf (buf, bufsize, "%s:%u", uuid, matchtag) >= bufsize) {
        flux_log_error (h, "failed to create matchtag key");
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
