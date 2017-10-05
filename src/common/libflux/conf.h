#ifndef _FLUX_CORE_CONF_H
#define _FLUX_CORE_CONF_H

enum {
    CONF_FLAG_INTREE=1,
};

const char *flux_conf_get (const char *name, int flags);

#endif /* !_FLUX_CORE_CONF_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
