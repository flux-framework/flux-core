/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_CORE_PLUGIN_H
#define FLUX_CORE_PLUGIN_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct flux_plugin flux_plugin_t;
typedef struct flux_plugin_arg flux_plugin_arg_t;

typedef int (*flux_plugin_f) (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args);

typedef int (*flux_plugin_init_f) (flux_plugin_t *p);

struct flux_plugin_handler {
    char *topic;
    flux_plugin_f cb;
};

/*  Create and destroy a flux_plugin_t handle
 */
flux_plugin_t * flux_plugin_create (void);
void flux_plugin_destroy (flux_plugin_t *p);

/*  Returns the last error from a plugin. Only valid if
 *   the last call returned an error.
 */
const char * flux_plugin_strerror (flux_plugin_t *p);

/*  Set a name for plugin 'p'. Overrides any existing name.
 */
int flux_plugin_set_name (flux_plugin_t *p, const char *name);

const char * flux_plugin_get_name (flux_plugin_t *p);

/*  Add a handler for topic 'topic' for the plugin 'p'.
 *  The topic string may be a glob to cause 'cb' to be invoked for
 *  a set of topic strings called by the host.
 */
int flux_plugin_add_handler (flux_plugin_t *p,
                             const char *topic,
                             flux_plugin_f cb);

/*  Remove handler associated with exact topic glob `topic`
 */
int flux_plugin_remove_handler (flux_plugin_t *p, const char *topic);

/*  Return handler that exactly matches topic glob `topic`.
 */
flux_plugin_f flux_plugin_get_handler (flux_plugin_t *p, const char *topic);

/*  Return handler that would match topic string `topic`, i.e. return
 *   the first handler in the list of handlers which would match topic
 *   string.
 */
flux_plugin_f flux_plugin_match_handler (flux_plugin_t *p, const char *topic);

/*  Convenience function to register a table of handlers along with
 *   a plugin name for the plugin 'p'.
 */
int flux_plugin_register (flux_plugin_t *p,
                          const char *name,
                          const struct flux_plugin_handler t[]);

/*  Associate auxillary data with the plugin handle 'p'. If free_fn is
 *   set then this function will be called on the data at plugin
 *   destruction.
 *
 *  If key == NULL, val != NULL, stores val for destruction, but it
 *   cannot be retrieved with flux_plugin_aux_get ().
 *  If key != NULL, val == NULL, destroys currently stored val.
 *  For a duplicate key, current val is destroyed and new value stored.
 */
int flux_plugin_aux_set (flux_plugin_t *p,
                         const char *key,
                         void *val,
                         flux_free_f free_fn);

/*  Get current auxillary data under `key`.
 */
void * flux_plugin_aux_get (flux_plugin_t *p, const char *key);


/*  Set optional JSON string as load-time config for plugin 'p'.
 */
int flux_plugin_set_conf (flux_plugin_t *p, const char *json_str);

/*  Read configuration for plugin 'p' using jansson style unpack args */
int flux_plugin_conf_unpack (flux_plugin_t *p, const char *fmt, ...);

/*  Create/destroy containers for marshalling read-only arguments
 *   and results between caller and plugin.
 */
flux_plugin_arg_t *flux_plugin_arg_create ();
void flux_plugin_arg_destroy (flux_plugin_arg_t *args);

const char *flux_plugin_arg_strerror (flux_plugin_arg_t *args);

/*  Flags for flux_plugin_arg_get/set/pack/unpack
 */
enum {
    FLUX_PLUGIN_ARG_IN =  0, /* Operate on input args  */
    FLUX_PLUGIN_ARG_OUT = 1  /* Operate on output args */
};

/*  Get/set arguments in plugin arg object using JSON encoded strings
 */
int flux_plugin_arg_set (flux_plugin_arg_t *args,
                         int flags,
                         const char *json_str);
int flux_plugin_arg_get (flux_plugin_arg_t *args,
                         int flags,
                         char **json_str);

/*  Pack/unpack arguments into plugin arg object using jansson pack style args
 */
int flux_plugin_arg_pack (flux_plugin_arg_t *args, int flags,
                          const char *fmt, ...);
int flux_plugin_arg_vpack (flux_plugin_arg_t *args, int flags,
                           const char *fmt, va_list ap);

int flux_plugin_arg_unpack (flux_plugin_arg_t *args, int flags,
                            const char *fmt, ...);
int flux_plugin_arg_vunpack (flux_plugin_arg_t *args, int flags,
                             const char *fmt, va_list ap);

/*  Call first plugin callback matching 'name', passing optional plugin
 *   arguments in 'args'.
 */
int flux_plugin_call (flux_plugin_t *p, const char *name,
                      flux_plugin_arg_t *args);

/*  Load a plugin from a shared object found in 'path'
 *
 *  Once the shared object is loaded, flux_plugin_init() is run from which
 *   DSO should register itself.
 *
 *  Returns -1 on failure to load plugin, or failure of flux_plugin_init().
 */
int flux_plugin_load_dso (flux_plugin_t *p, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_PLUGIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
