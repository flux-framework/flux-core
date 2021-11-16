/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* boot_config.c - get broker wireup info from config file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#include <jansson.h>
#include <sys/param.h>
#include <flux/core.h>
#include <flux/hostlist.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libpmi/clique.h"

#include "attr.h"
#include "overlay.h"
#include "boot_config.h"


/* Copy 'fmt' into 'buf', substituting the following tokens:
 * - %h  host
 * - %p  port
 * Returns 0 on success, or -1 on overflow.
 */
int boot_config_format_uri (char *buf,
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

/* Add a host entry to hosts for the hostname in 's', cloned from the
 * original host entry object.
 */
static int boot_config_append_host (json_t *hosts, const char *s, json_t *entry)
{
    json_t *nentry;
    json_t *o;

    if (!(nentry = json_deep_copy (entry)))
        goto nomem;
    if (!(o = json_string (s)))
        goto nomem;
    if (json_object_set_new (nentry, "host", o) < 0) {
        json_decref (o);
        goto nomem;
    }
    if (json_array_append_new (hosts, nentry) < 0)
        goto nomem;
    return 0;
nomem:
    json_decref (nentry);
    errno = ENOMEM;
    return -1;
}

/* Build a new hosts array, expanding any RFC29 hostlists, so that
 * json_array_size() is the number of hosts, and json_array_get() can
 * be used to fetch an entry by rank.  Caller must free.
 */
static json_t *boot_config_expand_hosts (json_t *hosts)
{
    json_t *nhosts;
    size_t index;
    json_t *value;

    if (!(nhosts = json_array ())) {
        log_msg ("Config file error [bootstrap]: out of memory");
        return NULL;
    }
    json_array_foreach (hosts, index, value) {
        struct hostlist *hl = NULL;
        const char *host, *s;

        if (json_unpack (value, "{s:s}", "host", &host) < 0) {
            log_msg ("Config file error [bootstrap]: missing host field");
            log_msg ("Hint: hosts entries must be table containing a host key");
            goto error;
        }
        if (!(hl = hostlist_decode (host))) {
            log_err ("Config file error [bootstrap]");
            log_msg ("Hint: host value '%s' is not a valid hostlist", host);
            goto error;
        }
        s = hostlist_first (hl);
        while (s) {
            if (boot_config_append_host (nhosts, s, value) < 0) {
                log_err ("Config file error [bootstrap]: appending host %s", s);
                hostlist_destroy (hl);
                goto error;
            }
            s = hostlist_next (hl);
        }
        hostlist_destroy (hl);
    }
    return nhosts;
error:
    json_decref (nhosts);

    return NULL;
}

/* Parse the [bootstrap] configuration.
 * Post-process conf->hosts to expand any embedded RFC29 hostlists and return
 * a new hosts JSON array on success.  On failure, log a human readable
 * message and return NULL.
 */
int boot_config_parse (const flux_conf_t *cf,
                       struct boot_conf *conf,
                       json_t **hostsp)
{
    flux_conf_error_t error;
    const char *default_bind = NULL;
    const char *default_connect = NULL;
    json_t *hosts = NULL;

    memset (conf, 0, sizeof (*conf));
    if (flux_conf_unpack (cf,
                          &error,
                          "{s:{s?s s?i s?s s?s s?o s?b}}",
                          "bootstrap",
                            "curve_cert", &conf->curve_cert,
                            "default_port", &conf->default_port,
                            "default_bind", &default_bind,
                            "default_connect", &default_connect,
                            "hosts", &conf->hosts,
                            "enable_ipv6", &conf->enable_ipv6) < 0) {
        log_msg ("Config file error [bootstrap]: %s", error.errbuf);
        return -1;
    }

    /* Take care of %p substitution in the default bind/connect URI's
     * If %h occurs in these values, it is preserved (since host=NULL here).
     */
    if (default_bind) {
        if (boot_config_format_uri (conf->default_bind,
                                    sizeof (conf->default_bind),
                                    default_bind,
                                    NULL,
                                    conf->default_port) < 0) {
            log_msg ("Config file error [bootstrap] %s",
                     "buffer overflow building default bind URI");
            return -1;
        }
    }
    if (default_connect) {
        if (boot_config_format_uri (conf->default_connect,
                                    sizeof (conf->default_connect),
                                    default_connect,
                                    NULL,
                                    conf->default_port) < 0) {
            log_msg ("Config file error [bootstrap] %s",
                     "buffer overflow building default connect URI");
            return -1;
        }
    }

    /* Expand any embedded hostlists in hosts entries.
     */
    if (conf->hosts) {
        if (!json_is_array (conf->hosts)) {
            log_msg ("Config file error [bootstrap] hosts must be array type");
            return -1;
        }
        if (json_array_size (conf->hosts) > 0) {
            if (!(hosts = boot_config_expand_hosts (conf->hosts)))
                return -1;
        }
    }

    /* Fail early if size > 1 and there is no CURVE certificate configured.
     */
    if (json_array_size (conf->hosts) > 1 && !conf->curve_cert) {
        log_msg ("Config file error [bootstrap] %s",
                 "curve_cert is required for size > 1");
        return -1;
    }

    *hostsp = hosts;
    return 0;
}

int boot_config_attr (attr_t *attrs, json_t *hosts)
{
    struct hostlist *hl = NULL;
    char *s = NULL;
    size_t index;
    json_t *value;
    char buf[1024];
    char *val;
    int rv = -1;

    if (!hosts || json_array_size (hosts) == 0)
        return 0;

    if (!(hl = hostlist_create ()))
        goto error;

    json_array_foreach (hosts, index, value) {
        const char *host;
        if (json_unpack (value, "{s:s}", "host", &host) < 0) {
            log_msg ("Internal error [bootstrap]: missing host field");
            errno = EINVAL;
            goto error;
        }
        if (hostlist_append (hl, host) < 0)
            goto error;
    }

    if (!(s = hostlist_encode (hl)))
        goto error;

    if (attr_add (attrs,
                  "broker.hostlist",
                  s,
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("attr_add broker.hostlist %s", s);
        goto error;
    }

    /* Generate broker.mapping.
     * For now, set it to NULL if there are multiple brokers per node.
     */
    hostlist_uniq (hl);
    if (hostlist_count (hl) < json_array_size (hosts))
        val = NULL;
    else {
        struct pmi_map_block mapblock = {
            .nodeid = 0,
            .nodes = json_array_size (hosts),
            .procs = 1
        };
        if (pmi_process_mapping_encode (&mapblock, 1, buf, sizeof (buf)) < 0) {
            log_msg ("encoding broker.mapping");
            errno = EOVERFLOW;
            goto error;
        }
        val = buf;
    }
    if (attr_add (attrs, "broker.mapping", val, FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr broker.mapping");
        goto error;
    }

    rv = 0;
error:
    hostlist_destroy (hl);
    free (s);
    return rv;
}

/* Find host 'name' in hosts array, and set 'rank' to its array index.
 * Return 0 on success, -1 on failure.
 */
int boot_config_getrankbyname (json_t *hosts, const char *name, uint32_t *rank)
{
    size_t index;
    json_t *entry;

    json_array_foreach (hosts, index, entry) {
        const char *host;

        /* N.B. missing host key already detected by boot_config_parse().
         */
        if (json_unpack (entry, "{s:s}", "host", &host) == 0
                    && !strcmp (name, host)) {
            *rank = index;
            return 0;
        }
    }
    log_msg ("Config file error [bootstrap]: %s not found in hosts", name);
    return -1;
}

static int gethostentry (json_t *hosts,
                         struct boot_conf *conf,
                         uint32_t rank,
                         const char **host,
                         const char **bind,
                         const char **uri)
{
    json_t *entry;

    if (!(entry = json_array_get (hosts, rank))) {
        log_msg ("Config file error [bootstrap] rank %u not found in hosts",
                 (unsigned int)rank);
        return -1;
    }
    /* N.B. missing host key already detected by boot_config_parse().
     */
    if (json_unpack (entry,
                     "{s:s s?:s s?:s}",
                     "host",
                     host,
                     "bind",
                     bind,
                     "connect",
                     uri) < 0) {
        log_msg ("Config file error [bootstrap]: rank %u bad hosts entry",
                 (unsigned int)rank);
        log_msg ("Hint: bind and connect keys, if present, are type string");
        return -1;
    }
    return 0;
}

/* Look up the host entry for 'rank', then copy that entry's bind address
 * into 'buf'.  If the entry doesn't provide an explicit bind address,
 * use the default.  Perform any host or port substitutions while copying.
 * Return 0 on success, -1 on failure.
 */
int boot_config_getbindbyrank (json_t *hosts,
                               struct boot_conf *conf,
                               uint32_t rank,
                               char *buf,
                               int bufsz)
{
    const char *uri = NULL;
    const char *bind = NULL;
    const char *host = NULL;

    if (gethostentry (hosts, conf, rank, &host, &bind, &uri) < 0)
        return -1;
    if (!bind)
        bind = conf->default_bind;
    if (strlen (bind) == 0) {
        log_msg ("Config file error [bootstrap]: rank %u missing bind URI",
                 (unsigned int)rank);
        return -1;
    }
    if (boot_config_format_uri (buf,
                                bufsz,
                                bind,
                                host,
                                conf->default_port) < 0) {
        log_msg ("Config file error [bootstrap]: buffer overflow");
        return -1;
    }
    return 0;
}

/* Look up the host entry for 'rank', then copy that entry's connect address
 * into 'buf'.  If the entry doesn't provide an explicit connect address,
 * use the default.  Perform any host or port substitutions while copying.
 * Return 0 on success, -1 on failure.
 */
int boot_config_geturibyrank (json_t *hosts,
                              struct boot_conf *conf,
                              uint32_t rank,
                              char *buf,
                              int bufsz)
{
    const char *uri = NULL;
    const char *bind = NULL;
    const char *host = NULL;

    if (gethostentry (hosts, conf, rank, &host, &bind, &uri) < 0)
        return -1;
    if (!uri)
        uri = conf->default_connect;
    if (strlen (uri) == 0) {
        log_msg ("Config file error [bootstrap]: rank %u missing connect URI",
                 (unsigned int)rank);
        return -1;
    }
    if (boot_config_format_uri (buf,
                                bufsz,
                                uri,
                                host,
                                conf->default_port) < 0) {
        log_msg ("Config file error [bootstrap]: buffer overflow");
        return -1;
    }
    return 0;
}

int boot_config (flux_t *h, struct overlay *overlay, attr_t *attrs)
{
    struct boot_conf conf;
    uint32_t rank;
    uint32_t size;
    int fanout = overlay_get_fanout (overlay);
    json_t *hosts = NULL;

    /* Ingest the [bootstrap] stanza.
     */
    if (boot_config_parse (flux_get_conf (h), &conf, &hosts) < 0)
        return -1;

    if (boot_config_attr (attrs, hosts) < 0)
        goto error;

    /* If hosts array was specified, match hostname to determine rank,
     * and size is the length of the hosts array.  O/w rank=0, size=1.
     */
    if (hosts != NULL) {
        const char *fakehost = getenv ("FLUX_FAKE_HOSTNAME"); // for testing;
        char hostname[MAXHOSTNAMELEN + 1];

        if (gethostname (hostname, sizeof (hostname)) < 0) {
            log_err ("gethostbyname");
            goto error;
        }
        if (boot_config_getrankbyname (hosts,
                                      fakehost ? fakehost : hostname,
                                      &rank) < 0)
            goto error;
        size = json_array_size (hosts);
    }
    else {
        size = 1;
        rank = 0;
    }

    /* Tell overlay network this broker's rank and size.
     * If a curve certificate was provided, load it.
     */
    if (overlay_set_geometry (overlay, size, rank) < 0)
        goto error;
    if (conf.curve_cert) {
        if (overlay_cert_load (overlay, conf.curve_cert) < 0)
            goto error; // prints error
    }

    /* If user requested ipv6, enable it here.
     * N.B. this prevents binding from interfaces that are IPv4 only (#3824).
     */
    overlay_set_ipv6 (overlay, conf.enable_ipv6);

    /* If broker has "downstream" peers, determine the URI to bind to
     * from the config and tell overlay.  Also, set the tbon.endpoint
     * attribute to the URI peers will connect to.  If broker has no
     * downstream peers, set tbon.endpoint to NULL.
     */
    if (kary_childof (fanout, size, rank, 0) != KARY_NONE) {
        char bind_uri[MAX_URI + 1];
        char my_uri[MAX_URI + 1];

        assert (hosts != NULL);

        if (boot_config_getbindbyrank (hosts,
                                       &conf,
                                       rank,
                                       bind_uri,
                                       sizeof (bind_uri)) < 0)
            goto error;
        if (overlay_bind (overlay, bind_uri) < 0)
            goto error;
        if (overlay_authorize (overlay,
                               overlay_cert_name (overlay),
                               overlay_cert_pubkey (overlay)) < 0) {
            log_err ("overlay_authorize");
            goto error;
        }
        if (boot_config_geturibyrank (hosts,
                                      &conf,
                                      rank,
                                      my_uri,
                                      sizeof (my_uri)) < 0)
            goto error;
        if (attr_add (attrs,
                      "tbon.endpoint",
                      my_uri,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0) {
            log_err ("setattr tbon.endpoint %s", my_uri);
            goto error;
        }
    }
    else {
        if (attr_add (attrs,
                      "tbon.endpoint",
                      NULL,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0) {
            log_err ("setattr tbon.endpoint NULL");
            goto error;
        }
    }

    /* If broker has an "upstream" peer, determine its URI and tell overlay.
     */
    if (rank > 0) {
        char parent_uri[MAX_URI + 1];
        if (boot_config_geturibyrank (hosts,
                                      &conf,
                                      kary_parentof (fanout, rank),
                                      parent_uri,
                                      sizeof (parent_uri)) < 0)
            goto error;
        if (overlay_set_parent_uri (overlay, parent_uri) < 0) {
            log_err ("overlay_set_parent_uri %s", parent_uri);
            goto error;
        }
        if (overlay_set_parent_pubkey (overlay,
                                       overlay_cert_pubkey (overlay)) < 0) {
            log_err ("overlay_set_parent_pubkey self");
            goto error;
        }
    }

    /* instance-level (position in instance hierarchy) is always zero here.
     */
    if (attr_add (attrs,
                  "instance-level",
                  "0",
                  FLUX_ATTRFLAG_IMMUTABLE) < 0) {
        log_err ("setattr instance-level 0");
        goto error;
    }
    json_decref (hosts);
    return 0;
error:
    ERRNO_SAFE_WRAP (json_decref, hosts);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
