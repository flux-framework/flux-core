/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_BOOT_CONFIG_H
#define BROKER_BOOT_CONFIG_H

/* boot_config - bootstrap broker/overlay from config file */

#include "attr.h"
#include "overlay.h"

/* Broker attributes read/written directly by this method:
 *   tbon.endpoint (w)
 *   instance-level (w)
 */
int boot_config (flux_t *h,
                 const char *hostname,
                 struct overlay *overlay,
                 attr_t *attrs);

/* The following is exported for unit testing.
 */
#define MAX_URI 2048

struct boot_conf {
    const char *curve_cert;
    int default_port;
    char default_bind[MAX_URI + 1];
    char default_connect[MAX_URI + 1];
    json_t *hosts;
    int enable_ipv6;
};

int boot_config_geturibyrank (json_t *hosts,
                              struct boot_conf *conf,
                              uint32_t rank,
                              char *buf,
                              int bufsz);
int boot_config_getbindbyrank (json_t *hosts,
                               struct boot_conf *conf,
                               uint32_t rank,
                               char *buf,
                               int bufsz);
int boot_config_getrankbyname (json_t *hosts,
                               const char *name,
                               uint32_t *rank);
int boot_config_parse (const flux_conf_t *cf,
                       struct boot_conf *conf,
                       json_t **hosts);
int boot_config_attr (attr_t *attrs, const char *hostname, json_t *hosts);
int boot_config_format_uri (char *buf,
                            int bufsz,
                            const char *fmt,
                            const char *host,
                            int port);

#endif /* BROKER_BOOT_CONFIG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
