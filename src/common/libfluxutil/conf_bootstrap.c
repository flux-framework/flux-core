/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* conf_bootstrap.c - parse and validate [bootstrap] table
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/hostlist.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libyuarel/yuarel.h"
#include "ccan/str/str.h"

#include "conf_bootstrap.h"

#define MAX_URI 2048

struct bootstrap_info {
    const char *curve_cert;
    int default_port;
    const char *default_bind;
    const char *default_connect;
    json_t *hosts;
    int enable_ipv6;
    int size;
    json_t *xhosts;
};

struct host_entry {
    const char *host;
    const char *bind;
    const char *connect;
    const char *parent;
};

/* Validate domain label (RFC 1035)
 */
static int validate_domain_label (const char *s, flux_error_t *errp)
{
    size_t len = strlen (s);

    if (len < 1 || len > 63)
        return errprintf (errp, "must be 1-63 characters");
    if (s[0] == '-' || s[len - 1] == '-')
        return errprintf (errp, "domain label must not start or end with '-'");
    for (int i = 0; i < len; i++) {
        if (!isalnum (s[i]) && s[i] != '-') {
            return errprintf (errp,
                              "domain label contains invalid character"
                              " '%c' (0x%x)",
                              s[i],
                              s[i]);
        }
    }
    return 0;
}

/* Validate domain name (RFC 1035)
 */
int conf_bootstrap_validate_domain_name (const char *host, flux_error_t *errp)
{
    size_t len = strlen (host);
    char cpy[254] = { 0 };

    if (len < 1 || len > 253)
        return errprintf (errp, "domain must be 1-253 characters");
    strncpy (cpy, host, sizeof (cpy) - 1);

    char *tok;
    char *cp = cpy;
    char *saveptr = NULL;
    while ((tok = strtok_r (cp, ".", &saveptr))) {
        if (validate_domain_label (tok, errp) < 0)
            return -1;
        cp = NULL;
    }
    return 0;
}

/* Copy 'fmt' into 'buf', substituting the following tokens:
 * - %h  host
 * - %p  port
 * Returns 0 on success, or -1 on overflow.
 */
int conf_bootstrap_format_uri (char *buf,
                               int bufsz,
                               const char *fmt,
                               const char *host,
                               int port)
{
    const char *cp;
    int len = 0;
    bool tok_flag = false;
    int n;

    cp = fmt;
    while (*cp) {
        if (tok_flag) {
            switch (*cp) {
            case 'h': /* %h - host */
                if (host) {
                    n = snprintf (&buf[len], bufsz - len, "%s", host);
                    if (n >= bufsz - len)
                        goto overflow;
                    len += n;
                }
                else {
                    buf[len++] = '%';
                    buf[len++] = 'h';
                }
                break;
            case 'p': /* %p - port */
                n = snprintf (&buf[len], bufsz - len, "%d", port);
                if (n >= bufsz - len)
                    goto overflow;
                len += n;
                break;
            case '%': /* %% - literal % */
                buf[len++] = '%';
                break;
            default:
                buf[len++] = '%';
                buf[len++] = *cp;
                break;
            }
            tok_flag = false;
        }
        else {
            if (*cp == '%')
                tok_flag = true;
            else
                buf[len++] = *cp;
        }
        if (len >= bufsz)
            goto overflow;
        cp++;
    }
    buf[len] = '\0';
    return 0;
overflow:
    return -1;
}

/* Validate 0MQ URI.
 */
int conf_bootstrap_validate_zmq_uri (const char *uri, flux_error_t *errp)
{
    if (uri) {
        size_t len = strlen (uri);
        if (!strstr (uri, "://"))
            return errprintf (errp, "%s has no URI scheme", uri);
        if (strstr (uri, ":*"))
            return errprintf (errp, "%s contains 0MQ port wildcard", uri);
        if (strstarts (uri, "ipc://")) {
            if (len - 6 < 1)
                return errprintf (errp, "%s socket path is empty", uri);
            if (len - 6 > 108) {
                return errprintf (errp,
                                  "%s socket path must not exceed 108 bytes",
                                  uri);
            }
        }
        else if (strstarts (uri, "tcp://")) {
            if (len - 6 < 1)
                return errprintf (errp, "%s address is empty", uri);
        }
        else
            return errprintf (errp, "%s URI scheme must be tcp or ipc", uri);
    }
    return 0;
}

static int set_string (json_t *o, const char *key, const char *s)
{
    json_t *val;

    if (!(val = json_string (s))
        || json_object_set_new (o, key, val) < 0) {
        json_decref (val);
        return -1;
    }
    return 0;
}

/* Parse one element of the hosts array, substituting defaults for missing
 * fields and performing token substitution.  Then validate the entry.
 * Return -1 on failure, 0 on success
 */
static int parse_hosts_entry (struct bootstrap_info *binfo,
                              json_t *entry,
                              json_t *hobj,
                              flux_error_t *errp)
{
    struct host_entry he = { 0 };
    json_error_t jerror;
    flux_error_t error;

    if (json_unpack_ex (entry,
                        &jerror,
                        0,
                        "{s:s s?s s?s s?s !}",
                        "host", &he.host,
                        "bind", &he.bind,
                        "connect", &he.connect,
                        "parent", &he.parent) < 0)
        return errprintf (errp, "%s", jerror.text);
    if (he.parent) {
        if (streq (he.parent, he.host))
            return errprintf (errp, "parent key refers to self");
        if (!json_object_get (hobj, he.parent))
            return errprintf (errp, "parent key refers to unknown host");
    }
    char bind_uri[MAX_URI + 1] = { 0 };
    char connect_uri[MAX_URI + 1] = { 0 };

    if (conf_bootstrap_validate_domain_name (he.host, &error) < 0)
        return errprintf (errp, "%s %s", he.host, error.text);
    if (he.bind || binfo->default_bind) {
        const char *tmpl = he.bind ? he.bind : binfo->default_bind;
        if (conf_bootstrap_format_uri (bind_uri,
                                       sizeof (bind_uri),
                                       tmpl,
                                       he.host,
                                       binfo->default_port) < 0)
            return errprintf (errp, "bind key is too long");
        if (conf_bootstrap_validate_zmq_uri (bind_uri, &error) < 0)
            return errprintf (errp, "bind key %s", error.text);
        if (set_string (entry, "bind", bind_uri) < 0)
            return errprintf (errp, "out of memory");
    }
    if (he.connect || binfo->default_connect) {
        const char *tmpl = he.connect ? he.connect : binfo->default_connect;
        if (conf_bootstrap_format_uri (connect_uri,
                                       sizeof (connect_uri),
                                       tmpl,
                                       he.host,
                                       binfo->default_port) < 0) {
            return errprintf (errp,
                              "connect key is too long");
        }
        if (conf_bootstrap_validate_zmq_uri (connect_uri, &error) < 0)
            return errprintf (errp, "connect key %s", error.text);
        if (set_string (entry, "connect", connect_uri) < 0)
            return errprintf (errp, "out of memory");
    }
    return 0;
}

static json_t *rehost_entry (json_t *entry, const char *host)
{
    json_t *cpy;
    json_t *o = NULL;

    if (!(cpy = json_deep_copy (entry))
        || !(o = json_string (host))
        || json_object_set_new (cpy, "host", o) < 0) {
        json_decref (o);
        json_decref (cpy);
        return NULL;
    }
    return cpy;
}

/* Parse entry, expanding 'host' as a hostlist.
 * For each host, insert entry into hobj or if host already has
 * an entry add any missing keys from the new entry.
 * In addition to the hobj hash, append each (new) entry to binfo->xhosts
 * to retain the host order.
 * Validation comes later.
 */
static int dedup_hosts_entry (struct bootstrap_info *binfo,
                              json_t *entry,
                              json_t *hobj,
                              flux_error_t *errp)
{
    const char *host;
    json_error_t jerror;
    struct hostlist *hl = NULL;

    if (json_unpack_ex (entry, &jerror, 0, "{s:s}", "host", &host) < 0)
        return errprintf (errp, "%s", jerror.text);
    if (!(hl = hostlist_decode (host)))
        return errprintf (errp, "host key is not a valid RFC 29 hostlist");
    host = hostlist_first (hl);
    while (host) {
        json_t *obj;
        if ((obj = json_object_get (hobj, host))) {
            if (json_object_update_missing (obj, entry) < 0)
                goto nomem;
        }
        else {
            if (!(obj = rehost_entry (entry, host))
                || json_object_set_new (hobj, host, obj) < 0) {
                json_decref (obj);
                goto nomem;
            }
            if (json_array_append (binfo->xhosts, obj) < 0)
                goto nomem;
        }
        host = hostlist_next (hl);
    }
    hostlist_destroy (hl);
    return 0;
nomem:
    errprintf (errp, "out of memory");
    hostlist_destroy (hl);
    return -1;
}

/* Parse and validate the hosts array
 * Assign the expanded hosts array to binfo->xhosts.
 * Return -1 on failure, instance size on success.
 */
static int parse_hosts (json_t *hosts,
                        const char *hostname,
                        struct bootstrap_info *binfo,
                        flux_error_t *errp)
{
    size_t index;
    json_t *entry;
    json_t *hobj = NULL;
    flux_error_t error;

    if (hosts && !json_is_array (hosts))
        return errprintf (errp, "must be array type");
    if (!(binfo->xhosts = json_array ()))
        goto nomem;
    // missing or empty hosts array is allowed - assume singleton
    if (!hosts || json_array_size (hosts) == 0) {
        if (!(entry = json_pack ("{s:s}", "host", hostname))
            || json_array_append_new (binfo->xhosts, entry) < 0) {
            json_decref (entry);
            goto nomem;
        }
        return 1;
    }
    if (!(hobj = json_object ())) // just a tool for deduplication
        goto nomem;
    // first pass - expand hostlists and combine duplicates => binfo->xhosts
    json_array_foreach (hosts, index, entry) {
        if (dedup_hosts_entry (binfo, entry, hobj, &error) < 0) {
            errprintf (errp, "invalid hosts[%zu]: %s", index, error.text);
            goto error;
        }
    }
    // second pass - apply defaults and validate
    json_array_foreach (binfo->xhosts, index, entry) {
        if (parse_hosts_entry (binfo, entry, hobj, &error) < 0) {
            errprintf (errp, "invalid hosts[%zu]: %s", index, error.text);
            goto error;
        }
    }
    // ensure local hostname is is present
    if (!json_object_get (hobj, hostname)) {
        errprintf (errp, "%s not found in hosts", hostname);
        goto error;
    }
    json_decref (hobj);
    return json_array_size (binfo->xhosts);
nomem:
    errprintf (errp, "out of memory");
error:
    json_decref (hobj);
    json_decref (binfo->xhosts);
    binfo->xhosts = NULL;
    return -1;
}

/* Check that TCP port number is usable.
 */
static int validate_port (int port, flux_error_t *errp)
{
    if (port != -1) {
        if (port < 0 || port > 65535)
            return errprintf (errp, "must be in the range of 0-65535");
    }
    return 0;
}

static int validate_curve_cert (const char *path, int size, flux_error_t *errp)
{
    if (size > 1 && !path)
        return errprintf (errp, "curve_cert must be defined for size > 1");
    return 0;
}

int conf_bootstrap_parse (const flux_conf_t *conf,
                          const char *hostname,
                          bool *enable_ipv6,
                          const char **curve_cert,
                          json_t **hosts,
                          flux_error_t *errp)
{
    flux_error_t error;
    json_error_t jerror;
    json_t *bootstrap = NULL;
    struct bootstrap_info binfo = { .default_port = -1 };

    if (flux_conf_unpack (conf, &error, "{s?o}", "bootstrap", &bootstrap) < 0)
        return errprintf (errp,
                          "Config file error [bootstrap]: %s",
                          error.text);
    if (bootstrap) {
        if (json_unpack_ex (bootstrap,
                            &jerror,
                            0,
                            "{s?o s?s s?i s?s s?s s?b !}",
                            "hosts", &binfo.hosts,
                            "curve_cert", &binfo.curve_cert,
                            "default_port", &binfo.default_port,
                            "default_bind", &binfo.default_bind,
                            "default_connect", &binfo.default_connect,
                            "enable_ipv6", &binfo.enable_ipv6) < 0) {
            return errprintf (errp,
                              "Config file error [bootstrap]: %s",
                              jerror.text);
        }
        if (validate_port (binfo.default_port, &error) < 0) {
            return errprintf (errp,
                              "Config file error [bootstrap]: default_port %s",
                              error.text);
        }
        // allocates binfo.xhosts on success
        if ((binfo.size = parse_hosts (binfo.hosts,
                                       hostname,
                                       &binfo,
                                       &error)) < 0) {
            return errprintf (errp,
                              "Config file error [bootstrap]: %s",
                              error.text);
        }
        if (validate_curve_cert (binfo.curve_cert, binfo.size, &error) < 0) {
            json_decref (binfo.xhosts);
            return errprintf (errp,
                              "Config file error [bootstrap]: %s",
                              error.text);
        }
    }
    if (enable_ipv6)
        *enable_ipv6 = binfo.enable_ipv6 ? true : false;
    if (curve_cert)
        *curve_cert = binfo.curve_cert;
    if (hosts)
        *hosts = binfo.xhosts;
    else
        json_decref (binfo.xhosts);
    return 0;
}

// vi:ts=4 sw=4 expandtab
