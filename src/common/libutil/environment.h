/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_ENVIRONMENT_H
#define _UTIL_ENVIRONMENT_H

#include <stdbool.h>
#include <stdarg.h>

/* Create/destroy environment
 */
struct environment *environment_create (void);
void environment_destroy (struct environment *e);

/**
 * @brief Set the iteration cursor for the environment to the first element
 *
 * @param e the environment to iterate over
 *
 * @return the key of the first element or null if the environment is empty
 */
const char *environment_first (struct environment *e);

/**
 * @brief Get the next key in the environment
 *
 * @param e the environment to continue iterating over
 *
 * @return the key of the next element or null if iteration is complete or the
 * environment is empty
 */
const char *environment_next (struct environment *e);

/**
 * @brief Get the value of the current cursor element, matching the key from
 * either *_first or *_next
 *
 * @param e the envirnoment to operate on
 *
 * @return The value of the current environment element
 */
const char *environment_cursor (struct environment *e);

/**
 * @brief Apply the changes encoded in this environment to the environment of
 * the current process.
 *
 * All keys with values of non-zero length are set to those values, keys whose
 * values are strings of length 0 are explicitly unset.  Other keys in the
 * environment remain unchanged.
 *
 * @param e the environment that should be applied
 */
void environment_apply (struct environment *e);

/**
 * @brief Split, deduplicate, and push a new value onto the front of the
 * environment variable whose key is "key."
 *
 * @param e the environment to add this element to
 * @param key the environment variable name
 * @param value The value to use, this will be split based on the separator
 * defined for the key if one is set, otherwise it is prepended whole
 */
void environment_push (struct environment *e, const char *key, const char *value);

/**
 * @brief Split, deduplicate, and push a new value onto the back of the
 * environment variable whose key is "key."
 *
 * @param e the environment to add this element to
 * @param key the environment variable name
 * @param value The value to use, this will be split based on the separator
 * defined for the key if one is set, otherwise it is appended whole
 */
void environment_push_back (struct environment *e, const char *key, const char *value);

/**
 * @brief Add the specified value to the front of the target key without
 * de-duplication, it will still be separated from the rest of the value by sep,
 * if a sep has been set for this key.
 *
 * Generally, the de-duplicating functions are preferred to these, but if a
 * value you must include contains separator characters, use these.
 *
 * @param e The environment to operate on
 * @param key The key to target
 * @param value The value to prepend
 */
void environment_no_dedup_push (struct environment *e,
                                const char *key,
                                const char *value);

/**
 * @brief Add the specified value to the target key without de-duplication, it
 * will still be separated from the rest of the value by sep, if a sep has
 * been set for this key.
 *
 * @param e The environment to operate on
 * @param key The key to target
 * @param value The value to append
 */
void environment_no_dedup_push_back (struct environment *e,
                                     const char *key,
                                     const char *value);

/**
 * @brief Set the environment value "key" to "value," where elements are
 * separated by "sep," overwrites whatever value is currently set.
 *
 * @param e the environment to act on
 * @param key the key to target
 * @param value the value to set
 * @param separator the separator used to divide elements in the value, ":"
 * for unix paths for example, or ";" for LUA_PATH
 */
void environment_set (struct environment *e,
                      const char *key,
                      const char *value,
                      char separator);

/**
 * @brief Explicitly unset the environment variable "key," this will cause the
 * variable to be cleared when this environment spec is applied.
 *
 * @param e the environment to act on
 * @param key the key which should be unset on application
 */
void environment_unset (struct environment *e, const char *key);

/**
 * @brief Initialize the environment variable "key" with the value from the
 * enclosing environment if it is set, otherwise set it to "default_base."
 *
 * @param e the environment to act on
 * @param key the key to initialize
 * @param default_base the default value to use if the environment variable is
 * unset
 * @param separator the separator between tokens in the value
 */
void environment_from_env (struct environment *e,
                           const char *key,
                           const char *default_base,
                           char separator);

/**
 * @brief Set the separator for a given environment variable.
 *
 * @param e the environment to act on
 * @param key the environment variable to update
 * @param separator the separator to use to join components of this object and
 * split and dedup inputs
 */
void environment_set_separator (struct environment *e, const char *key, char separator);

/**
 * @brief Get the value of a given environment variable by name.
 *
 * This does *not* check the environment of the current process, but the
 * configured environment specified by 'e'.
 *
 * @param e the environment to act on
 * @param key key of the environment variable to read
 *
 * @return A reference to the value of the key, or null if that key is not set
 */
const char *environment_get (struct environment *e, const char *key);

#endif /* !_UTIL_ENVIRONMENT */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
