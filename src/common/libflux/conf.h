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

/**
 * @brief Set the iteration cursor for the environment to the first element
 *
 * @param cf the config containing the environment to iterate over
 *
 * @return the key of the first element or null if the environment is empty
 */
const char *flux_conf_environment_first (flux_conf_t cf);
/**
 * @brief Get the next key in the environment
 *
 * @param cf the config to continue iterating over
 *
 * @return the key of the next element or null if iteration is complete or the
 * environment is empty
 */
const char *flux_conf_environment_next (flux_conf_t cf);
/**
 * @brief Get the value of the current cursor element, matching the key from
 * either *_first or *_next
 *
 * @param cf the config to operate on
 *
 * @return The value of the current environment element
 */
const char *flux_conf_environment_cursor (flux_conf_t cf);

/**
 * @brief Apply the changes encoded in this flux_conf_t to the environment of
 * the current process.
 *
 * All keys with values of non-zero length are set to those values, keys whose
 * values are strings of length 0 are explicitly unset.  Other keys in the
 * environment remain unchanged.
 *
 * @param cf  The configuration whose environment should be applied
 */
void flux_conf_environment_apply (flux_conf_t cf);
/**
 * @brief Split, deduplicate, and push a new value onto the front of the
 * environment variable whose key is "key."
 *
 * @param cf the config to add this element to
 * @param key the environment variable name
 * @param value The value to use, this will be split based on the separator
 * defined for the key if one is set, otherwise it is prepended whole
 */
void flux_conf_environment_push (flux_conf_t cf,
                                 const char *key,
                                 const char *value);
/**
 * @brief Split, deduplicate, and push a new value onto the back of the
 * environment variable whose key is "key."
 *
 * @param cf the config to add this element to
 * @param key the environment variable name
 * @param value The value to use, this will be split based on the separator
 * defined for the key if one is set, otherwise it is appended whole
 */
void flux_conf_environment_push_back (flux_conf_t cf,
                                      const char *key,
                                      const char *value);
/**
 * @brief Add the specified value to the front of the target key without
 * de-duplication, it will still be separated from the rest of the value by sep,
 * if a sep has been set for this key.
 *
 * Generally, the de-duplicating functions are preferred to these, but if a
 * value you must include contains separator characters, use these.
 *
 * @param cf The config to operate on
 * @param key The key to target
 * @param value The value to prepend
 */
void flux_conf_environment_no_dedup_push (flux_conf_t cf,
                                 const char *key,
                                 const char *value);
/**
 * @brief Add the specified value to the target key without de-duplication, it
 * will still be separated from the rest of the value by sep, if a sep has
 * been set for this key.
 *
 * @param cf The config to operate on
 * @param key The key to target
 * @param value The value to append
 */
void flux_conf_environment_no_dedup_push_back (flux_conf_t cf,
                                      const char *key,
                                      const char *value);
/**
 * @brief Set the environment value "key" to "value," where elements are
 * separated by "sep," overwrites whatever value is currently set.
 *
 * @param cf the config to act on
 * @param key the key to target
 * @param value the value to set
 * @param separator the separator used to divide elements in the value, ":"
 * for unix paths for example, or ";" for LUA_PATH
 */
void flux_conf_environment_set (flux_conf_t cf,
                                const char *key,
                                const char *value,
                                const char *separator);
/**
 * @brief Explicitly unset the environment variable "key," this will cause the
 * variable to be cleared when this environment spec is applied.
 *
 * @param cf the config to act on
 * @param key the key which should be unset on application
 */
void flux_conf_environment_unset (flux_conf_t cf, const char *key);
/**
 * @brief Initialize the environment variable "key" with the value from the
 * enclosing environment if it is set, otherwise set it to "default_base."
 *
 * @param cf the config to act on
 * @param key the key to initialize
 * @param default_base the default value to use if the environment variable is
 * unset
 * @param separator the separator between tokens in the value
 */
void flux_conf_environment_from_env (flux_conf_t cf,
                                     const char *key,
                                     const char *default_base,
                                     const char *separator);
/**
 * @brief Set the separator for a given environment variable.
 *
 * @param cf the config to act on
 * @param key the environment variable to update
 * @param separator the separator to use to join components of this object and
 * split and dedup inputs
 */
void flux_conf_environment_set_separator (flux_conf_t cf,
                                          const char *key,
                                          const char *separator);

/**
 * @brief Get the value of a given environment variable by name.
 *
 * This does *not* check the environment of the current process, but the
 * configured environment in the passed flux_conf_t.
 *
 * @param cf the config to act on
 * @param key key of the environment variable to read
 *
 * @return A reference to the value of the key, or null if that key is not set
 */
const char *flux_conf_environment_get (flux_conf_t cf, const char *key);

#endif /* !_FLUX_CORE_FCONFIG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
