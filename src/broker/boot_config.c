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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <jansson.h>
#include <sys/param.h>
#include <flux/core.h>
#include <flux/hostlist.h>
#include <flux/taskmap.h>

#include "src/common/libyuarel/yuarel.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "attr.h"
#include "overlay.h"
#include "topology.h"
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

/* Make a copy of 'entry', set host key to the specified value, and append
 * to 'hosts' array.
 */
static int boot_config_append_host (json_t *hosts,
                                    const char *hostname,
                                    json_t *entry)
{
    json_t *nentry;

    if (!(nentry = json_deep_copy (entry))
        || set_string (nentry, "host", hostname) < 0
        || json_array_append_new (hosts, nentry) < 0) {
        json_decref (nentry);
        return -1;
    }
    return 0;
}

/* Build a new hosts array, expanding any RFC29 hostlists, so that
 * json_array_size() is the number of hosts, and json_array_get() can
 * be used to fetch an entry by rank.  Preserve the initial hosts order so
 * that the rank mapping is deterministic, but combine the host entries of
 * any entries that have the same host key.
 * The caller must release the returned JSON object with json_decref().
 */
static json_t *boot_config_expand_hosts (json_t *hosts)
{
    json_t *nhosts = NULL;
    json_t *hash = NULL;
    size_t index;
    json_t *value;

    if (!(nhosts = json_array ())
        || !(hash = json_object ())) {
        log_msg ("Config file error [bootstrap]: out of memory");
        goto error;
    }
    json_array_foreach (hosts, index, value) {
        struct hostlist *hl = NULL;
        json_error_t error;
        const char *host, *s;
        const char *bind = NULL;
        const char *connect = NULL;
        const char *parent = NULL;

        if (json_unpack_ex (value,
                            &error,
                            0,
                            "{s:s s?s s?s s?s !}",
                            "host", &host,
                            "bind", &bind,
                            "connect", &connect,
                            "parent", &parent) < 0) {
            log_msg ("Config file error [bootstrap] host entry: %s",
                     error.text);
            goto error;
        }
        if (!(hl = hostlist_decode (host))) {
            log_msg ("Config file error [bootstrap]:"
                     " host value '%s' is not a valid hostlist",
                     host);
            goto error;
        }
        s = hostlist_first (hl);
        while (s) {
            json_t *entry;
            if (!(entry = json_object_get (hash, s))) {
                if (boot_config_append_host (nhosts, s, value) < 0
                    || json_object_set (hash, s, value) < 0) {
                    log_msg ("Config file error [bootstrap]:"
                             " error appending host %s", s);
                    hostlist_destroy (hl);
                    goto error;
                }
            }
            else {
                if (json_object_update (entry, value) < 0) {
                    log_msg ("Config file error [bootstrap]:"
                             " error merging host %s", s);
                    hostlist_destroy (hl);
                    goto error;
                }
            }
            s = hostlist_next (hl);
        }
        hostlist_destroy (hl);
    }
    json_decref (hash);
    return nhosts;
error:
    json_decref (nhosts);
    json_decref (hash);

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
    flux_error_t error;
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
        log_msg ("Config file error [bootstrap]: %s", error.text);
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

int boot_config_attr (attr_t *attrs, const char *hostname, json_t *hosts)
{
    struct hostlist *hl = NULL;
    struct taskmap *map = NULL;
    char *s = NULL;
    size_t index;
    json_t *value;
    char *val;
    int rv = -1;

    if (!hosts || json_array_size (hosts) == 0) {
        if (attr_add (attrs,
                      "hostlist",
                      hostname,
                      ATTR_IMMUTABLE) < 0) {
            log_err ("failed to set hostlist attribute to localhost");
            goto error;
        }
        return 0;
    }

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
                  "hostlist",
                  s,
                  ATTR_IMMUTABLE) < 0) {
        log_err ("failed to set hostlist attribute to config derived value");
        goto error;
    }
    free (s);
    s = NULL;

    /* Generate broker.mapping.
     * For now, set it to NULL if there are multiple brokers per node.
     */
    hostlist_uniq (hl);
    if (hostlist_count (hl) < json_array_size (hosts))
        val = NULL;
    else {
        if (!(map = taskmap_create ())
            || taskmap_append (map, 0, json_array_size (hosts), 1) < 0)
            goto error;
        if (!(s = taskmap_encode (map, 0))) {
            log_msg ("encoding broker.mapping");
            errno = EOVERFLOW;
            goto error;
        }
        val = s;
    }
    if (attr_add (attrs, "broker.mapping", val, ATTR_IMMUTABLE) < 0) {
        log_err ("setattr broker.mapping");
        goto error;
    }

    rv = 0;
error:
    taskmap_destroy (map);
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

        /* N.B. entry already validated by boot_config_parse().
         */
        if (json_unpack (entry, "{s:s}", "host", &host) == 0
            && streq (name, host)) {
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
    /* N.B. entry already validated by boot_config_parse().
     */
    (void)json_unpack (entry,
                      "{s:s s?s s?s}",
                      "host", host,
                      "bind", bind,
                      "connect", uri);
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

static int set_broker_boot_method_attr (attr_t *attrs, const char *value)
{
    (void)attr_delete (attrs, "broker.boot-method", true);
    if (attr_add (attrs,
                  "broker.boot-method",
                  value,
                  ATTR_IMMUTABLE) < 0) {
        log_err ("setattr broker.boot-method");
        return -1;
    }
    return 0;
}

/* Zeromq treats failed hostname resolution as a transient error, and silently
 * retries in the background, which can make config problems hard to diagnose.
 * Parse the URI in advance, and if the host portion is invalid, log it.
 * Ref: flux-framework/flux-core#5009
 */
static void warn_of_invalid_host (flux_t *h, const char *uri)
{
    char *cpy;
    struct yuarel u = { 0 };
    struct addrinfo *result;
    int e;

    if (!(cpy = strdup (uri))
        || yuarel_parse (&u, cpy) < 0
        || !u.scheme
        || !u.host
        || !streq (u.scheme, "tcp"))
        goto done;
    /* N.B. this URI will be used for zmq_connect(), therefore it must
     * be a valid peer address, not an interface name or wildcard.
     */
    if ((e = getaddrinfo (u.host, NULL, NULL, &result)) == 0) {
        freeaddrinfo (result);
        goto done;
    }
    log_msg ("Warning: unable to resolve upstream peer %s: %s",
             u.host,
             gai_strerror (e));
done:
    free (cpy);
}

int boot_config (flux_t *h,
                 const char *hostname,
                 struct overlay *overlay,
                 attr_t *attrs)
{
    struct boot_conf conf;
    uint32_t rank;
    uint32_t size;
    json_t *hosts = NULL;
    struct topology *topo = NULL;
    flux_error_t error;
    const char *topo_uri;

    /* Ingest the [bootstrap] stanza.
     */
    if (boot_config_parse (flux_get_conf (h), &conf, &hosts) < 0)
        return -1;

    if (boot_config_attr (attrs, hostname, hosts) < 0)
        goto error;

    /* If hosts array was specified, match hostname to determine rank,
     * and size is the length of the hosts array.  O/w rank=0, size=1.
     */
    if (hosts != NULL) {
        if (boot_config_getrankbyname (hosts, hostname, &rank) < 0)
            goto error;
        size = json_array_size (hosts);
    }
    else {
        size = 1;
        rank = 0;
    }

    // N.B. overlay_create() sets the tbon.topo attribute
    if (attr_get (attrs, "tbon.topo", &topo_uri, NULL) < 0) {
        log_msg ("error fetching tbon.topo attribute");
        goto error;
    }
    topology_hosts_set (hosts);
    topo = topology_create (topo_uri, size, &error);
    topology_hosts_set (NULL);
    if (!topo) {
        log_msg ("Error creating %s topology: %s", topo_uri, error.text);
        goto error;
    }
    if (topology_set_rank (topo, rank) < 0
        || overlay_set_topology (overlay, topo) < 0)
        goto error;

    /* If a curve certificate was provided, load it.
     */
    if (conf.curve_cert) {
        if (overlay_cert_load (overlay, conf.curve_cert) < 0)
            goto error; // prints error
    }

    /* If user requested ipv6, enable it here.
     * N.B. this prevents binding from interfaces that are IPv4 only (#3824).
     */
    overlay_set_ipv6 (overlay, conf.enable_ipv6);

    /* Ensure that tbon.interface-hint is set.
     */
    if (overlay_set_tbon_interface_hint (overlay, NULL) < 0) {
        log_err ("error setting tbon.interface-hint attribute");
        goto error;
    }

    /* If broker has "downstream" peers, determine the URI to bind to
     * from the config and tell overlay.  Also, set the tbon.endpoint
     * attribute to the URI peers will connect to.  If broker has no
     * downstream peers, set tbon.endpoint to NULL.
     */
    if (topology_get_child_ranks (topo, NULL, 0) > 0
        && attr_get (attrs, "broker.recovery-mode", NULL, NULL) < 0) {
        char bind_uri[MAX_URI + 1];
        char my_uri[MAX_URI + 1];

        if (!hosts)
            log_err_exit ("internal error: hosts object is NULL");
        if (boot_config_getbindbyrank (hosts,
                                       &conf,
                                       rank,
                                       bind_uri,
                                       sizeof (bind_uri)) < 0)
            goto error;
        if (overlay_bind (overlay, bind_uri, NULL) < 0)
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
                      ATTR_IMMUTABLE) < 0) {
            log_err ("setattr tbon.endpoint %s", my_uri);
            goto error;
        }
    }
    else {
        if (attr_add (attrs,
                      "tbon.endpoint",
                      NULL,
                      ATTR_IMMUTABLE) < 0) {
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
                                      topology_get_parent (topo),
                                      parent_uri,
                                      sizeof (parent_uri)) < 0)
            goto error;
        warn_of_invalid_host (h, parent_uri);
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
                  ATTR_IMMUTABLE) < 0) {
        log_err ("setattr instance-level 0");
        goto error;
    }
    if (set_broker_boot_method_attr (attrs, "config") < 0)
        goto error;
    json_decref (hosts);
    topology_decref (topo);
    return 0;
error:
    ERRNO_SAFE_WRAP (json_decref, hosts);
    ERRNO_SAFE_WRAP (topology_decref, topo);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
