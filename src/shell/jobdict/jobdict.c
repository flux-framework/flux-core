/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jobdict.c - builtin shell plugin providing job-scoped dictionaries
 *
 * A jobdict is an ephemeral key-value dictionary shared by all shells of a
 * job.  Keys put on one shell become visible on other shells after an
 * exchange that both shells participate in.  Process managers (PMI, PMIx)
 * implement their bootstrap key exchange on top of it, and it may serve other
 * plugins that need to exchange small amounts of data among the shells of a
 * job.
 *
 * Other shell plugins (builtin or external) use a jobdict through the plugin
 * stack, so no C interface beyond flux/shell.h is needed.  Since
 * flux_shell_plugstack_call() succeeds when no plugin handles a topic,
 * create and exchange set {name:s} in the OUT args on success so a caller
 * can detect that this plugin is not loaded.
 *
 *   jobdict.create  IN {name:s k?:i reach?:[s...]}  OUT {name:s} | {errmsg:s}
 *     Create a dictionary.  'name' is also the shell service name used for
 *     inter-shell RPCs, so it must be unique within the shell.  'k' is the
 *     exchange tree fanout (default 2).  'reach' lists other dictionaries
 *     whose keys may be read through this one (see jobdict.get).
 *
 *   jobdict.put     IN {name:s key:s value:o local?:b}  OUT {} | {errmsg:s}
 *     Store 'value' (any json value) under 'key'.  The key is exchanged at
 *     the next exchange unless 'local' is true, in which case it is visible
 *     only on this shell (e.g. for shell-specific pre-populated keys).
 *
 *   jobdict.get     IN {name:s key:s}  OUT {value:o} | {} | {errmsg:s}
 *     Look up 'key' in dictionary 'name'.  If not found there and 'key' is
 *     of the form "<other>.<subkey>" where 'other' is in this dictionary's
 *     reach list, look up 'subkey' in dictionary 'other'.  If the key is
 *     not found, the OUT args are empty and the call succeeds; 'errmsg' is
 *     set only for hard errors.
 *
 *   jobdict.exchange IN {name:s tag:s participants?:s exit_topic:s}
 *                    OUT {name:s} | {errmsg:s}
 *     Exchange keys put since the last exchange among 'participants' (an
 *     RFC 22 idset of shell ranks, or all shells if omitted).  Every
 *     participant must be a valid shell rank and this shell MUST be one.
 *     'tag' distinguishes concurrent exchanges on the same dictionary.
 *     When the exchange completes on this shell,
 *     'exit_topic' is called through the plugin stack with
 *         IN {name:s tag:s result:o}   on success, or
 *         IN {name:s tag:s errmsg:s}   on failure,
 *     where 'result' is the dictionary of keys exchanged.  Since the
 *     exchange sends RPCs to other shells, it must not be called before
 *     shell.post-init, which follows the init barrier that guarantees all
 *     shell services are registered.
 *
 * Exchange semantics follow RFC 13 barrier semantics: keys exchanged among
 * all shells are then visible everywhere.  Keys exchanged among a subset of
 * shells are visible within that subset, and are also carried forward to the
 * next exchange among all shells so that they eventually become visible
 * everywhere.
 *
 * Since a put from a non-participating task may occur while an exchange is
 * in flight, each exchange contributes a snapshot of the pending keys and,
 * once complete, retires only those keys from the pending set.
 */
#define FLUX_SHELL_PLUGIN_NAME "jobdict"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "builtins.h"
#include "exchange.h"

struct jobdict {
    flux_shell_t *shell;
    char *name;
    int size;                   // number of shells in the job
    json_t *global;             // keys exchanged with all shells
    json_t *pending;            // keys not yet exchanged with all shells
    json_t *locals;             // keys never exchanged
    json_t *reach;              // array of dict names readable via prefix
    struct exchange *exchange;
};

/* Per-exchange state, passed through the exchange engine as the exit callback
 * arg.
 */
struct jobdict_exchange {
    struct jobdict *dict;
    json_t *snapshot;           // pending keys contributed to this exchange
    char *exit_topic;
    bool full;                  // exchange spans all shells
};

struct jobdict_ctx {
    flux_shell_t *shell;
    zhashx_t *dicts;            // struct jobdict hashed by name
};

/* Set {errmsg:s} in the OUT args and return -1, for plugin handlers.
 */
__attribute__ ((format (printf, 2, 3)))
static int seterror (flux_plugin_arg_t *args, const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    va_start (ap, fmt);
    (void)vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    (void)flux_plugin_arg_pack (args,
                                FLUX_PLUGIN_ARG_OUT,
                                "{s:s}",
                                "errmsg", buf);
    return -1;
}

static void jobdict_exchange_destroy (struct jobdict_exchange *xcg)
{
    if (xcg) {
        int saved_errno = errno;
        json_decref (xcg->snapshot);
        free (xcg->exit_topic);
        free (xcg);
        errno = saved_errno;
    }
}

static struct jobdict_exchange *jobdict_exchange_create (struct jobdict *dict,
                                                         const char *exit_topic,
                                                         bool full)
{
    struct jobdict_exchange *xcg;

    if (!(xcg = calloc (1, sizeof (*xcg))))
        return NULL;
    xcg->dict = dict;
    xcg->full = full;
    if (!(xcg->exit_topic = strdup (exit_topic)))
        goto error;
    if (!(xcg->snapshot = json_copy (dict->pending))) {
        errno = ENOMEM;
        goto error;
    }
    return xcg;
error:
    jobdict_exchange_destroy (xcg);
    return NULL;
}

/* Retire keys exchanged in a completed full exchange from the pending set,
 * unless they were re-put with a different value while the exchange was in
 * flight (in which case the new value still needs exchanging).
 */
static void retire_pending (struct jobdict *dict, json_t *snapshot)
{
    const char *key;
    json_t *val;

    json_object_foreach (snapshot, key, val) {
        if (json_object_get (dict->pending, key) == val)
            json_object_del (dict->pending, key);
    }
}

/* Deliver the exchange outcome to the plugin that requested it by calling
 * its exit topic through the plugin stack.
 */
static void exchange_exit (struct jobdict_exchange *xcg,
                          const char *tag,
                          json_t *result,
                          const char *errmsg)
{
    struct jobdict *dict = xcg->dict;
    flux_plugin_arg_t *args;
    int rc;

    if (!(args = flux_plugin_arg_create ())) {
        shell_warn ("%s: error creating exit args for %s", dict->name, tag);
        return;
    }
    if (errmsg) {
        rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_IN,
                                   "{s:s s:s s:s}",
                                   "name", dict->name,
                                   "tag", tag,
                                   "errmsg", errmsg);
    }
    else {
        rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_IN,
                                   "{s:s s:s s:O}",
                                   "name", dict->name,
                                   "tag", tag,
                                   "result", result);
    }
    if (rc < 0)
        shell_warn ("%s: error packing exit args for %s", dict->name, tag);
    else if (flux_shell_plugstack_call (dict->shell, xcg->exit_topic, args) < 0)
        shell_warn ("%s: %s failed for %s", dict->name, xcg->exit_topic, tag);
    flux_plugin_arg_destroy (args);
}

/* exchange_exit_f footprint */
static void exchange_exit_cb (struct exchange *engine,
                              const char *tag,
                              void *arg)
{
    struct jobdict_exchange *xcg = arg;
    struct jobdict *dict = xcg->dict;
    json_t *result;

    if (exchange_has_error (engine, tag)) {
        exchange_exit (xcg, tag, NULL, "exchange failed");
        goto done;
    }
    result = exchange_get_result (engine, tag);
    if (json_object_update (dict->global, result) < 0) {
        exchange_exit (xcg, tag, NULL, "out of memory merging exchange result");
        goto done;
    }
    if (xcg->full)
        retire_pending (dict, xcg->snapshot);
    exchange_exit (xcg, tag, result, NULL);
done:
    jobdict_exchange_destroy (xcg);
}

static void jobdict_destroy (struct jobdict *dict)
{
    if (dict) {
        int saved_errno = errno;
        exchange_destroy (dict->exchange);
        json_decref (dict->global);
        json_decref (dict->pending);
        json_decref (dict->locals);
        json_decref (dict->reach);
        free (dict->name);
        free (dict);
        errno = saved_errno;
    }
}

/* zhashx_destructor_fn footprint */
static void jobdict_destructor (void **item)
{
    if (item) {
        jobdict_destroy (*item);
        *item = NULL;
    }
}

static struct jobdict *jobdict_create (flux_shell_t *shell,
                                       const char *name,
                                       int k,
                                       json_t *reach)
{
    struct jobdict *dict;

    if (!(dict = calloc (1, sizeof (*dict))))
        return NULL;
    dict->shell = shell;
    dict->reach = json_incref (reach);
    if (!(dict->name = strdup (name)))
        goto error;
    if (!(dict->global = json_object ())
        || !(dict->pending = json_object ())
        || !(dict->locals = json_object ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_shell_info_unpack (shell, "{s:i}", "size", &dict->size) < 0)
        goto error;
    if (!(dict->exchange = exchange_create (shell, name, k)))
        goto error;
    return dict;
error:
    jobdict_destroy (dict);
    return NULL;
}

static int op_create (flux_plugin_t *p,
                      const char *topic,
                      flux_plugin_arg_t *args,
                      void *data)
{
    struct jobdict_ctx *ctx = data;
    const char *name;
    int k = 0;
    json_t *reach = NULL;
    size_t index;
    json_t *entry;
    struct jobdict *dict;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s?i s?o}",
                                "name", &name,
                                "k", &k,
                                "reach", &reach) < 0)
        return seterror (args,
                         "error unpacking jobdict.create arguments: %s",
                         flux_plugin_arg_strerror (args));
    if (reach && !json_is_array (reach))
        return seterror (args, "reach must be an array of dictionary names");
    if (reach) {
        json_array_foreach (reach, index, entry) {
            if (!json_is_string (entry))
                return seterror (args,
                                 "reach must be an array of dictionary names");
        }
    }
    if (zhashx_lookup (ctx->dicts, name))
        return seterror (args, "jobdict '%s' already exists", name);
    if (!(dict = jobdict_create (ctx->shell, name, k, reach)))
        return seterror (args,
                         "error creating jobdict '%s': %s",
                         name,
                         strerror (errno));
    if (zhashx_insert (ctx->dicts, name, dict) < 0) {
        jobdict_destroy (dict);
        return seterror (args, "error saving jobdict '%s'", name);
    }
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "{s:s}", "name", name)
        < 0)
        return seterror (args, "error packing jobdict.create result");
    return 0;
}

static int op_put (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct jobdict_ctx *ctx = data;
    const char *name;
    const char *key;
    json_t *value;
    int local = 0;
    struct jobdict *dict;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s s:o s?b}",
                                "name", &name,
                                "key", &key,
                                "value", &value,
                                "local", &local) < 0)
        return seterror (args,
                         "error unpacking jobdict.put arguments: %s",
                         flux_plugin_arg_strerror (args));
    if (!(dict = zhashx_lookup (ctx->dicts, name)))
        return seterror (args, "unknown jobdict '%s'", name);
    if (json_object_set (local ? dict->locals : dict->pending, key, value) < 0)
        return seterror (args, "out of memory storing key '%s'", key);
    return 0;
}

/* Look up 'key' in one dictionary: local keys, then keys pending exchange,
 * then keys already exchanged.
 */
static json_t *dict_lookup (struct jobdict *dict, const char *key)
{
    json_t *value;

    if (!(value = json_object_get (dict->locals, key))
        && !(value = json_object_get (dict->pending, key)))
        value = json_object_get (dict->global, key);
    return value;
}

/* Look up a name-qualified key "<other>.<key>" in dictionary <other>, if
 * <other> is in the reach list of 'dict'.
 */
static json_t *qualified_lookup (struct jobdict_ctx *ctx,
                                 struct jobdict *dict,
                                 const char *key)
{
    const char *dot;
    size_t len;
    size_t index;
    json_t *entry;

    if (!dict->reach
        || !(dot = strchr (key, '.'))
        || dot == key
        || dot[1] == '\0')
        return NULL;
    len = dot - key;
    json_array_foreach (dict->reach, index, entry) {
        const char *name = json_string_value (entry);
        struct jobdict *other;

        if (name
            && strlen (name) == len
            && strncmp (name, key, len) == 0
            && (other = zhashx_lookup (ctx->dicts, name)))
            return dict_lookup (other, dot + 1);
    }
    return NULL;
}

static int op_get (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct jobdict_ctx *ctx = data;
    const char *name;
    const char *key;
    json_t *value;
    struct jobdict *dict;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s}",
                                "name", &name,
                                "key", &key) < 0)
        return seterror (args,
                         "error unpacking jobdict.get arguments: %s",
                         flux_plugin_arg_strerror (args));
    if (!(dict = zhashx_lookup (ctx->dicts, name)))
        return seterror (args, "unknown jobdict '%s'", name);
    if (!(value = dict_lookup (dict, key))
        && !(value = qualified_lookup (ctx, dict, key)))
        return 0; // not found: OUT args left empty
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:O}",
                              "value", value) < 0)
        return seterror (args, "error packing jobdict.get result");
    return 0;
}

static int op_exchange (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct jobdict_ctx *ctx = data;
    const char *name;
    const char *tag;
    json_t *participants_o = NULL;
    const char *participants_str = NULL;
    const char *exit_topic;
    struct jobdict *dict;
    struct idset *participants = NULL;
    struct jobdict_exchange *xcg = NULL;
    bool full;

    /* N.B. 'participants' is unpacked as an object so callers may pack
     * either a string or (per jansson "s?" pack semantics) a json null.
     */
    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s s?o s:s}",
                                "name", &name,
                                "tag", &tag,
                                "participants", &participants_o,
                                "exit_topic", &exit_topic) < 0)
        return seterror (args,
                         "error unpacking jobdict.exchange arguments: %s",
                         flux_plugin_arg_strerror (args));
    if (!(dict = zhashx_lookup (ctx->dicts, name)))
        return seterror (args, "unknown jobdict '%s'", name);
    if (participants_o && !json_is_null (participants_o)) {
        if (!(participants_str = json_string_value (participants_o)))
            return seterror (args, "participants must be a string");
        if (!(participants = idset_decode (participants_str)))
            return seterror (args,
                             "invalid participants '%s'",
                             participants_str);
        if (idset_count (participants) == 0
            || idset_last (participants) >= dict->size) {
            seterror (args,
                      "participants '%s' is empty or exceeds shell count %d",
                      participants_str,
                      dict->size);
            goto error;
        }
    }
    /* Ranks are now known to be distinct and in 0..size-1, so a set of
     * 'size' ranks is exactly the set of all shells.
     */
    full = (!participants || idset_count (participants) == dict->size);
    if (!(xcg = jobdict_exchange_create (dict, exit_topic, full))) {
        seterror (args, "error creating exchange: %s", strerror (errno));
        goto error;
    }
    if (exchange_enter (dict->exchange,
                        tag,
                        participants,
                        xcg->snapshot,
                        exchange_exit_cb,
                        xcg) < 0) {
        seterror (args,
                  "error entering exchange '%s' on jobdict '%s': %s",
                  tag,
                  name,
                  strerror (errno));
        goto error;
    }
    idset_destroy (participants);
    if (flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT, "{s:s}", "name", name)
        < 0)
        return seterror (args, "error packing jobdict.exchange result");
    return 0;
error:
    jobdict_exchange_destroy (xcg);
    idset_destroy (participants);
    return -1;
}

static void jobdict_ctx_destroy (struct jobdict_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhashx_destroy (&ctx->dicts);
        free (ctx);
        errno = saved_errno;
    }
}

static struct jobdict_ctx *jobdict_ctx_create (flux_shell_t *shell)
{
    struct jobdict_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->shell = shell;
    if (!(ctx->dicts = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (ctx->dicts, jobdict_destructor);
    return ctx;
error:
    jobdict_ctx_destroy (ctx);
    return NULL;
}

static int plugin_init (flux_plugin_t *p)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    struct jobdict_ctx *ctx;

    if (!(ctx = jobdict_ctx_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p,
                             "jobdict",
                             ctx,
                             (flux_free_f)jobdict_ctx_destroy) < 0) {
        jobdict_ctx_destroy (ctx);
        return -1;
    }
    if (flux_plugin_add_handler (p, "jobdict.create", op_create, ctx) < 0
        || flux_plugin_add_handler (p, "jobdict.put", op_put, ctx) < 0
        || flux_plugin_add_handler (p, "jobdict.get", op_get, ctx) < 0
        || flux_plugin_add_handler (p,
                                    "jobdict.exchange",
                                    op_exchange,
                                    ctx) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_jobdict = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .plugin_init = plugin_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
