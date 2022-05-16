static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct conf *conf = arg;
    const flux_conf_t *instance_conf;
    struct conf_callback *ccb;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &instance_conf) < 0)
        goto error;
    if (policy_validate (instance_conf, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    ccb = zlistx_first (conf->callbacks);
    while (ccb) {
        if (ccb->cb (instance_conf, &error, ccb->arg) < 0) {
            errstr = error.text;
            errno = EINVAL;
            goto error;
        }
        ccb = zlistx_next (conf->callbacks);
    }
    if (flux_set_conf (h, flux_conf_incref (instance_conf)) < 0) {
        errstr = "error updating cached configuration";
        flux_conf_decref (instance_conf);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error: 
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.config-reload", config_reload_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};





static int process_config (struct kvs_ctx *ctx)
{
    flux_error_t error;
    if (kvs_checkpoint_config_parse (ctx->kcp,
                                     flux_get_conf (ctx->h),
                                     &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s", error.text);
        return -1;
    }
    return 0;
}



static int checkpoint_period_parse (const flux_conf_t *conf,
                                    flux_error_t *errp,
                                    double *checkpoint_period)
{
    flux_error_t error;
    const char *str = NULL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s}}",
                          "kvs",
                          "checkpoint-period", &str) < 0) {
        errprintf (errp,
                   "error reading config for kvs: %s",
                   error.text);
        return -1;
    }

    if (str) {
        if (fsd_parse_duration (str, checkpoint_period) < 0) {
            errprintf (errp,
                       "invalid checkpoint-period config: %s",
                       str);
            return -1;
        }
    }

    return 0;
}

int kvs_checkpoint_config_parse (kvs_checkpoint_t *kcp,
                                 const flux_conf_t *conf,
                                 flux_error_t *errp)
{
    if (kcp) {
        double checkpoint_period = kcp->checkpoint_period;
        if (checkpoint_period_parse (conf, errp, &checkpoint_period) < 0)
            return -1;
        kcp->checkpoint_period = checkpoint_period;
    }
    return 0;
}

