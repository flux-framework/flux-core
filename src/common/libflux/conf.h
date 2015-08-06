#ifndef _FLUX_CORE_CONF_H
#define _FLUX_CORE_CONF_H

#include <stdbool.h>
#include <stdarg.h>

/* Config file is formatted as ZPL
 *   http://rfc.zeromq.org/spec:4/ZPL
 * In the API below, keys are actually fully qualfied paths into the ZPL
 * hierarchy, using "." as the path separator (not "/" like zconfig).
 */

typedef struct flux_conf_struct *flux_conf_t;
typedef struct flux_conf_itr_struct *flux_conf_itr_t;

/* Create/destroy config context.
 */
flux_conf_t flux_conf_create (void);
void flux_conf_destroy (flux_conf_t cf);

/* Get/set config directory.  If unset, a default in the user's
 * home directory is used.  The config _file_ is hardwired to
 * $configdir/config.
 */
const char *flux_conf_get_directory (flux_conf_t cf);
void flux_conf_set_directory (flux_conf_t cf, const char *path);

/* Load/save/clear the cached config.
 */
int flux_conf_load (flux_conf_t cf);
int flux_conf_save (flux_conf_t cf);
void flux_conf_clear (flux_conf_t cf);

/* Get/put config values using the cached config.
 */
const char *flux_conf_get (flux_conf_t cf, const char *key);
int flux_conf_put (flux_conf_t cf, const char *key, const char *fmt, ...);

/* Iterate over keys in the cached config.  Fully qualified key paths are
 * returned, suitable for passing to flux_conf_get ().
 */
flux_conf_itr_t flux_conf_itr_create (flux_conf_t cf);
void flux_conf_itr_destroy (flux_conf_itr_t itr);
const char *flux_conf_next (flux_conf_itr_t itr);

const char *flux_conf_environment_first (flux_conf_t cf);
const char *flux_conf_environment_next (flux_conf_t cf);
const char *flux_conf_environment_cursor (flux_conf_t cf);

void flux_conf_environment_apply (flux_conf_t cf);
void flux_conf_environment_push (flux_conf_t cf, const char *key,
        const char *value);
void flux_conf_environment_push_back (flux_conf_t cf, const char *key,
        const char *value);
void flux_conf_environment_set (flux_conf_t cf, const char *key,
        const char *value, const char *separator);
void flux_conf_environment_unset (flux_conf_t cf, const char *key);
void flux_conf_environment_from_env(flux_conf_t cf, const char *key, const
        char *default_base, const char *separator);
void flux_conf_environment_set_separator(flux_conf_t cf, const char *key,
        const char *separator);
const char *flux_conf_environment_get (flux_conf_t cf, const char *key);



#endif /* !_FLUX_CORE_FCONFIG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
