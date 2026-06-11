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
#include <zmq.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"
#include "ccan/str/str.h"

#include "ovconf.h"

static const char *default_interface_hint = "default-route";

static const double default_torpid_min = 5.0;
static const double default_torpid_max = 30.0;

/* How long to wait (seconds) for a peer broker's TCP ACK before disconnecting.
 * This can be configured via TOML and on the broker command line.
 */
static const double default_tcp_user_timeout = 20.;

/* How long to wait (seconds) for a connect attempt to time out before
 * reconnecting.
 */
static const double default_connect_timeout = 30.;



static int ovconf_attr_int (flux_t *h,
                            const char *name,
                            int default_value,
                            int *valuep,
                            flux_error_t *errp)
{
    int value = default_value;
    const char *val;
    char *endptr;
    char buf[32];

    if ((val = flux_attr_get (h, name))) {
        errno = 0;
        value = strtol (val, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            errno = EINVAL;
            return errprintf (errp, "%s value must be an integer", name);
        }
    }
    if (snprintf (buf, sizeof (buf), "%d", value) >= sizeof (buf)) {
        errno = EOVERFLOW;
        return errprintf (errp, "%s value too large", name);
    }
    if (flux_attr_set_ex (h, name, buf, true, NULL) < 0)
        return errprintf (errp, "attr_set %s: %s", name, strerror (errno));
    if (valuep)
        *valuep = value;
    return 0;
}

static int ovconf_tbon_int (flux_t *h,
                            const flux_conf_t *conf,
                            const char *name,
                            int *value,
                            int default_value,
                            flux_error_t *errp)
{
    char attrname[128];
    flux_error_t error;

    *value = default_value;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?i}}",
                          "tbon",
                            name, value) < 0) {
        errprintf (errp, "Config file error [tbon]: %s", error.text);
        return -1;
    }
    (void)snprintf (attrname, sizeof (attrname), "tbon.%s", name);
    if (ovconf_attr_int (h, attrname, *value, value, errp) < 0)
        return -1;
    return 0;
}


static int ovconf_tbon_timeout (flux_t *h,
                                const flux_conf_t *conf,
                                const char *name,
                                double default_value,
                                double *valuep,
                                flux_error_t *errp)
{
    const char *fsd = NULL;
    double value = default_value;
    char long_name[128];
    char buf[64];
    flux_error_t error;

    (void)snprintf (long_name, sizeof (long_name), "tbon.%s", name);

    if (flux_conf_unpack (conf, &error, "{s?{s?s}}", "tbon", name, &fsd) < 0) {
        return errprintf (errp,
                          "Config file error [tbon]: %s",
                          error.text);
    }
    if (fsd) {
        if (fsd_parse_duration (fsd, &value) < 0) {
            return errprintf (errp,
                              "Config file error parsing %s",
                              long_name);
        }
    }

    /* Override with broker attribute (command line only) settings, if any.
     */
    if ((fsd = flux_attr_get (h, long_name))) {
        if (fsd_parse_duration (fsd, &value) < 0)
            return errprintf (errp, "Error parsing %s attribute", long_name);
    }
    if (fsd_format_duration (buf, sizeof (buf), value) < 0)
        return errprintf (errp, "fsd format: %s", strerror (errno));
    if (flux_attr_set_ex (h, long_name, buf, true, NULL) < 0) {
        return errprintf (errp,
                          "attr_set %s: %s",
                          long_name,
                          strerror (errno));
    }
    *valuep = value;
    return 0;
}

/* Set tbon.interface-hint attribute with the following precedence:
 * 1. broker attribute
 * 2. TOML config
 * 3. legacy environment variables
 * Leave it unset if none of those are available.
 * The bootstrap methods set it later, but only if not already set.
 */
static int ovconf_interface_hint (flux_t *h,
                                  const flux_conf_t *conf,
                                  flux_error_t *errp)
{
    const char *name = "tbon.interface-hint";
    const char *val = NULL;
    const char *config_val = NULL;
    const char *attr_val = NULL;
    flux_error_t error;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s}}",
                          "tbon",
                            "interface-hint", &config_val) < 0) {
        return errprintf (errp,
                          "Config file error [%s]: %s",
                          "tbon",
                          error.text);
    }
    attr_val = flux_attr_get (h, name);

    if (attr_val)
        val = attr_val;
    else if (config_val)
        val = config_val;
    else if ((val = getenv ("FLUX_IPADDR_INTERFACE")))
        ;
    else if (getenv ("FLUX_IPADDR_HOSTNAME"))
        val = "hostname";
    else
        val = default_interface_hint;

    if (val && !attr_val) {
        if (flux_attr_set (h, name, val) < 0) {
            return errprintf (errp,
                              "Error setting %s attribute value: %s",
                              name,
                              strerror (errno));
         }
    }
    return 0;
}

/* Configure tbon.topo attribute.
 * Ascending precedence: compiled-in default, TOML config, command line.
 * Topology creation is deferred to bootstrap, when we know the instance size.
 */
static int ovconf_topo (flux_t *h, const flux_conf_t *conf, flux_error_t *errp)
{
    const char *topo_uri = "kary:32";
    flux_error_t error;

    if (flux_conf_unpack (conf, NULL, "{s:{}}", "bootstrap") == 0)
        topo_uri = "custom"; // adjust default for config boot

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s}}",
                          "tbon",
                            "topo", &topo_uri) < 0) {
        errprintf (errp, "Config file error [tbon]: %s", error.text);
        return -1;
    }
    /* Treat tbon.fanout=K as an alias for tbon.topo=kary:K.
     */
    const char *fanout;
    char buf[16];
    if ((fanout = flux_attr_get (h, "tbon.fanout"))) {
        snprintf (buf, sizeof (buf), "kary:%s", fanout);
        topo_uri = buf;
    }
    /* Set tbon.topo attribute if not already set.
     */
    if (!flux_attr_get (h, "tbon.topo")) {
        if (flux_attr_set (h, "tbon.topo", topo_uri) < 0) {
            errprintf (errp,
                       "Error setting tbon.topo attribute: %s",
                       strerror (errno));
            return -1;
        }
    }
    return 0;
}

static int ovconf_torpid (struct ovconf *ovconf,
                          flux_t *h,
                          const flux_conf_t *conf,
                          bool initialize,
                          flux_error_t *errp)
{
    flux_error_t error;
    double tmin_val = -1;
    double tmax_val = -1;
    const char *tmin;
    const char *tmax;
    char fsd[64];

    // compiled-in default
    if (initialize) {
        tmin_val = default_torpid_min;
        tmax_val = default_torpid_max;
    }

    // TOML config (initial and updates)
    tmin = tmax = NULL;
    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s s?s}}",
                          "tbon",
                            "torpid_min", &tmin,
                            "torpid_max", &tmax) < 0)
        return errprintf (errp, "Config file error [tbon]: %s", error.text);
    if (tmin && fsd_parse_duration (tmin, &tmin_val) < 0)
        return errprintf (errp, "Config file error parsing tbon.torpid_min");
    if (tmax && fsd_parse_duration (tmax, &tmax_val) < 0)
        return errprintf (errp, "Config file error parsing tbon.torpid_max");

    // command line attribute setting
    if (initialize) {
        if ((tmin = flux_attr_get (h, "tbon.torpid_min"))
            && fsd_parse_duration (tmin, &tmin_val) < 0)
            return errprintf (errp, "error parsing tbon.torpid_min attribute");
        if ((tmax = flux_attr_get (h, "tbon.torpid_max"))
            && fsd_parse_duration (tmax, &tmax_val) < 0)
            return errprintf (errp, "error parsing tbon.torpid_max attribute");
    }

    // update tbon.torpid_min
    if (tmin_val != -1) {
        if (tmin_val == 0)
            return errprintf (errp, "tbon.torpid_min must be nonzero");
        double tmax_check = tmax_val != -1 ? tmax_val : ovconf->torpid_max;
        if (tmax_check != 0 && !(tmin_val < tmax_check))
            return errprintf (errp, "tbon.torpid_min must be less than torpid_max");
        if (fsd_format_duration (fsd, sizeof (fsd), tmin_val) < 0
            || flux_attr_set_ex (h, "tbon.torpid_min", fsd, true, NULL) < 0)
            return errprintf (errp, "error updating tbon.torpid_min attribute");
        ovconf->torpid_min = tmin_val;
    }
    // update tbon.torpid_max
    if (tmax_val != -1) {
        double tmin_check = tmin_val != -1 ? tmin_val : ovconf->torpid_min;
        if (tmax_val != 0 && !(tmax_val > tmin_check))
            return errprintf (errp,
                              "tbon.torpid_max must be greater than torpid_min");
        if (fsd_format_duration (fsd, sizeof (fsd), tmax_val) < 0
            || flux_attr_set_ex (h, "tbon.torpid_max", fsd, true, NULL) < 0)
            return errprintf (errp, "error updating tbon.torpid_max attribute");
        ovconf->torpid_max = tmax_val;
    }
    return 0;
}

/* Handle a config update, topic: config-sync or config-reload.
 * In the former case, the update should be handled like the initial
 * config, where command line broker attribute settings override the config.
 * In the latter case, the config overrides.
 */
static void ovconf_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct ovconf *ovconf = arg;
    const char *topic = "unknown";
    flux_error_t error;
    flux_conf_t *conf = NULL;
    bool initialize = false;

    if (flux_request_decode (msg, &topic, NULL) < 0
        || flux_module_config_request_decode (msg, &conf) < 0) {
        errprintf (&error, "Failed to parse %s request", topic);
        goto error;
    }
    if (streq (topic, "overlay.config-sync"))
        initialize = true;
    if (ovconf_torpid (ovconf, h, conf, initialize, &error) < 0)
        goto error;
    if (flux_set_conf_new (h, conf) < 0) {
        errprintf (&error, "Failed to update config");
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to %s request", topic);
    return;
error:
    flux_conf_decref (conf);
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to %s request", topic);
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.config-reload",
        ovconf_reload_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.config-sync",
        ovconf_reload_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};


void ovconf_fini (struct ovconf *ovconf)
{
    if (ovconf) {
        flux_msg_handler_delvec (ovconf->handlers);
        ovconf->handlers = NULL;
    }
}

int ovconf_init (struct ovconf *ovconf, flux_t *h, flux_error_t *errp)
{
    const flux_conf_t *conf;
    struct ovconf ovconf_new = { 0 };

    if (!ovconf || !h) {
        errno = EINVAL;
        return errprintf (errp, "ovconf_init: invalid argument");
    }

    conf = flux_get_conf (h);

    if (ovconf_attr_int (h, "tbon.prefertcp", 0, NULL, errp) < 0
        || ovconf_interface_hint (h, conf, errp) < 0
        || ovconf_topo (h, conf, errp) < 0
        || ovconf_torpid (&ovconf_new, h, conf, true, errp) < 0
        || ovconf_tbon_timeout (h,
                                conf,
                                "tcp_user_timeout",
                                default_tcp_user_timeout,
                                &ovconf_new.tcp_user_timeout,
                                errp) < 0
        || ovconf_tbon_timeout (h,
                                conf,
                                "connect_timeout",
                                default_connect_timeout,
                                &ovconf_new.connect_timeout,
                                errp) < 0
        || ovconf_tbon_int (h,
                            conf,
                            "zmqdebug",
                            &ovconf_new.zmqdebug,
                            0,
                            errp) < 0
        || ovconf_tbon_int (h,
                            conf,
                            "child_rcvhwm",
                            &ovconf_new.child_rcvhwm,
                            0,
                            errp) < 0
        || ovconf_tbon_int (h,
                            conf,
                            "zmq_io_threads",
                            &ovconf_new.zmq_io_threads,
                            1,
                            errp) < 0)
            return -1;

    if (ovconf_new.child_rcvhwm < 0 || ovconf_new.child_rcvhwm == 1) {
        errprintf (errp, "tbon.child_rcvhwm must be 0 (unlimited) or >= 2");
        errno = EINVAL;
        return -1;
    }
    if (ovconf_new.zmq_io_threads < 1) {
        errprintf (errp, "tbon.zmq_io_threads must be >= 1");
        errno = EINVAL;
        return -1;
    }

    if (flux_msg_handler_addvec (h, htab, ovconf, &ovconf_new.handlers) < 0) {
        errprintf (errp,
                   "could not register config message handlers: %s",
                   strerror (errno));
        return -1;
    }

    *ovconf = ovconf_new;
    return 0;
}

void ovconf_set_ipv6 (struct ovconf *ovconf, int enable)
{
    if (ovconf)
        ovconf->enable_ipv6 = enable;
}

// vi:ts=4 sw=4 expandtab
