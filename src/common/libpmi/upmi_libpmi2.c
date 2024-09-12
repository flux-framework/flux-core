/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
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
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <link.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "ccan/base64/base64.h"

#include "simple_client.h"
#include "pmi_strerror.h"
#include "pmi2.h"
#include "upmi.h"
#include "upmi_plugin.h"

#define LIBPMI2_IS_CRAY_CRAY 1

struct plugin_ctx {
    void *dso;
    int (*init) (int *spawned, int *size, int *rank, int *appnum);
    int (*finalize) (void);
    int (*abort) (int flag, const char *msg);
    int (*job_getid) (char *jobid, int jobid_size);
    int (*kvs_put) (const char *key, const char *value);
    int (*kvs_fence) (void);
    int (*kvs_get) (const char *jobid,
                    int src_pmi_id,
                    const char *key,
                    char *value,
                    int maxvalue,
                    int *vallen);
    int (*getjobattr) (const char *name,
                       const char *value,
                       int valuelen,
                       int *found);
    int flags;
    int size;
    int rank;
    char jobid[256];
};

static const char *plugin_name = "libpmi2";

static const char *dlinfo_name (void *dso)
{
    struct link_map *p;
    if (dlinfo (dso, RTLD_DI_LINKMAP, &p) < 0)
        return NULL;
    return p->l_name;
}

static int dlopen_wrap (const char *path,
                        int flags,
                        bool noflux,
                        void **result,
                        flux_error_t *error)
{
    void *dso;

    dlerror ();
    if (!(dso = dlopen (path, flags))) {
        char *errstr = dlerror ();
        if (errstr)
            errprintf (error, "%s", errstr); // dlerror() already includes path
        else
            errprintf (error, "%s: dlopen failed", path);
        return -1;
    }
    if (noflux) {
        if (dlsym (dso, "flux_pmi_library") != NULL) {
            errprintf (error,
                       "%s: dlopen found Flux library (%s)",
                       path,
                       dlinfo_name (dso));
            goto error;
        }
    }
    *result = dso;
    return 0;
error:
    dlclose (dso);
    return -1;
}

static void plugin_ctx_destroy (struct plugin_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        if (ctx->dso)
            dlclose (ctx->dso);
        free (ctx);
        errno = saved_errno;
    }
}

static struct plugin_ctx *plugin_ctx_create (const char *path,
                                             bool noflux,
                                             bool craycray,
                                             flux_error_t *error)
{
    struct plugin_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (error, "out of memory");
        return NULL;
    }
    if (!path)
        path = "libpmi2.so";
    // Use RTLD_GLOBAL due to flux-framework/flux-core#432
    if (dlopen_wrap (path,
                     RTLD_NOW | RTLD_GLOBAL,
                     noflux,
                     &ctx->dso,
                     error) < 0)
        goto error;
    if (!(ctx->init = dlsym (ctx->dso, "PMI2_Init"))
        || !(ctx->finalize = dlsym (ctx->dso, "PMI2_Finalize"))
        || !(ctx->abort = dlsym (ctx->dso, "PMI2_Abort"))
        || !(ctx->job_getid = dlsym (ctx->dso, "PMI2_Job_GetId"))
        || !(ctx->kvs_put = dlsym (ctx->dso, "PMI2_KVS_Put"))
        || !(ctx->kvs_fence = dlsym (ctx->dso, "PMI2_KVS_Fence"))
        || !(ctx->kvs_get = dlsym (ctx->dso, "PMI2_KVS_Get"))
        || !(ctx->getjobattr = dlsym (ctx->dso, "PMI2_Info_GetJobAttr"))) {
        errprintf (error, "%s:  missing required PMI2_* symbols", path);
        goto error;
    }

    if (dlsym (ctx->dso, "PMI_CRAY_Get_app_size") != NULL || craycray)
        ctx->flags |= LIBPMI2_IS_CRAY_CRAY;

    return ctx;
error:
    if (ctx->dso)
        dlclose (ctx->dso);
    free (ctx);
    return NULL;
}

static void charsub (char *s, char c1, char c2)
{
    for (int i = 0; i < strlen (s); i++)
        if (s[i] == c1)
            s[i] = c2;
}

/* Base64 encode 's', then translate = to a character not in RFC 4648 base64
 * alphabet.  Caller must free result.
 */
static char *encode_cray_value (const char *s)
{
    size_t len = strlen (s);
    size_t bufsize = base64_encoded_length (len) + 1; // +1 for \0 termination
    char *buf;

    if (!(buf = malloc (bufsize)))
        return NULL;
    if (base64_encode (buf, bufsize, s, len) < 0) {
        free (buf);
        errno = EINVAL;
        return NULL;
    }
    charsub (buf, '=', '*');
    return buf;
}

/* Reverse the result of encode_cray_value().  Caller must free result.
 */
static char *decode_cray_value (const char *s)
{
    char *cpy;

    if (!(cpy = strdup (s)))
        return NULL;
    charsub (cpy, '*', '=');

    int len = strlen (cpy);
    size_t bufsize = base64_decoded_length (len) + 1; // +1 for \0 termination
    void *buf;

    if (!(buf = malloc (bufsize)))
        goto error;
    if (base64_decode (buf, bufsize, cpy, len) < 0) {
        errno = EINVAL;
        goto error;
    }
    free (cpy);
    return buf;
error:
    ERRNO_SAFE_WRAP (free, cpy);
    ERRNO_SAFE_WRAP (free, buf);
    return NULL;
}

static int op_put (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    const char *key;
    const char *value;
    char *xvalue = NULL;
    int result;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:s}",
                                "key", &key,
                                "value", &value) < 0)
        return upmi_seterror (p, args, "error unpacking put arguments");
    /* Workaround for flux-framework/flux-core#5040.
     * Cray's libpmi2.so doesn't like ; and = characters in KVS values.
     */
    if ((ctx->flags & LIBPMI2_IS_CRAY_CRAY)) {
        if (!(xvalue = encode_cray_value (value)))
            return upmi_seterror (p, args, "%s", strerror (errno));
    }

    result = ctx->kvs_put (key, xvalue ? xvalue : value);
    if (result != PMI2_SUCCESS) {
        free (xvalue);
        return upmi_seterror (p, args, "%s", pmi_strerror (result));
    }
    free (xvalue);
    return 0;
}

static int op_get (flux_plugin_t *p,
                   const char *topic,
                   flux_plugin_arg_t *args,
                   void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;
    const char *key;
    char value[1024];
    int vallen = 0; // ignored
    char *xvalue = NULL;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "key", &key) < 0)
        return upmi_seterror (p, args, "error unpacking get arguments");

    /* Divert PMI_process_mapping to a job attribute request.
     */
    if (streq (key, "PMI_process_mapping")) {
        int found = 0;
        result = ctx->getjobattr (key, value, sizeof (value), &found);
        if (result == PMI2_SUCCESS && !found)
            result = PMI2_ERR_INVALID_KEY;
    }
    /* Workaround for flux-framework/flux-core#5040.
     * Cray's libpmi2.so prints to stderr when asked for a missing key.
     * To avoid that unpleasantness, short circuit the request for "flux."
     * prefixed keys, on the assumption that Cray's libpmi2.so won't ever be
     * employed by Flux to launch Flux.
     */
    else if ((ctx->flags & LIBPMI2_IS_CRAY_CRAY)
        && (strstarts (key, "flux.")))
        result = PMI2_ERR_INVALID_KEY;
    else {
        result = ctx->kvs_get (NULL,            // this job's key-value space
                               PMI2_ID_NULL,    // no hints for you
                               key,
                               value,
                               sizeof (value),
                               &vallen);
        if (result == PMI2_SUCCESS) {
            /* Workaround for flux-framework/flux-core#5040.
             * Cray's libpmi2.so doesn't like ; and = characters in KVS values.
             */
            if ((ctx->flags & LIBPMI2_IS_CRAY_CRAY)) {
                if (!(xvalue = decode_cray_value (value)))
                    return upmi_seterror (p, args, "%s", strerror (errno));
            }
        }
    }
    if (result != PMI2_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:s}",
                              "value", xvalue ? xvalue : value) < 0) {
        free (xvalue);
        return -1;
    }
    free (xvalue);
    return 0;
}

static int op_barrier (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int result;

    result = ctx->kvs_fence ();
    if (result != PMI2_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    return 0;
}

static int op_abort (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)

{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);
    int flag = 1; // abort all processes in the job
    const char *msg;
    int result;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s}",
                                "msg", &msg) < 0)
        return upmi_seterror (p, args, "error unpacking abort arguments");
    result = ctx->abort (flag, msg);
    if (result != PMI2_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));
    return 0;
}

static int op_initialize (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);

    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:i s:s s:i}",
                              "rank", ctx->rank,
                              "name", ctx->jobid,
                              "size", ctx->size) < 0)
        return -1;
    return 0;
}


static int op_finalize (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    struct plugin_ctx *ctx = flux_plugin_aux_get (p, plugin_name);

    int result;

    result = ctx->finalize ();
    if (result != PMI2_SUCCESS)
        return upmi_seterror (p, args, "%s", pmi_strerror (result));

    return 0;
}

static int op_preinit (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    struct plugin_ctx *ctx;
    flux_error_t error;
    const char *path = NULL;
    int noflux = 0;
    int craycray = 0;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s?s s?b s?b}",
                                "path", &path,
                                "noflux", &noflux,
                                "craycray", &craycray) < 0)
        return upmi_seterror (p, args, "error unpacking preinit arguments");
    if (!(ctx = plugin_ctx_create (path, noflux, craycray, &error)))
        return upmi_seterror (p, args, "%s", error.text);
    if (flux_plugin_aux_set (p,
                             plugin_name,
                             ctx,
                             (flux_free_f)plugin_ctx_destroy) < 0) {
        plugin_ctx_destroy (ctx);
        return upmi_seterror (p, args, "%s", strerror (errno));
    }

    /* Call PMI2_Init() and PMI2_Job_Getid() now so that upmi can fall
     * through to the next plugin on failure.
     */
    int spawned;
    int appnum;
    int result;
    const char *name = dlinfo_name (ctx->dso);

    if (!name)
        name = "unknown";

    result = ctx->init (&spawned, &ctx->size, &ctx->rank, &appnum);
    if (result != PMI2_SUCCESS)
        goto error;
    /* N.B. slurm's libpmi2 succeeds in PMI2_Init() but fails here
     * outside of a slurm job.  See: flux-framework/flux-core#5057
     */
    result = ctx->job_getid (ctx->jobid, sizeof (ctx->jobid));
    if (result != PMI2_SUCCESS)
        goto error;

    char note[1024];
    snprintf (note,
              sizeof (note),
              "using %s%s",
              name,
              (ctx->flags & LIBPMI2_IS_CRAY_CRAY)
                  ? " (cray quirks enabled)" : "");
    flux_plugin_arg_pack (args,
                          FLUX_PLUGIN_ARG_OUT,
                          "{s:s}",
                          "note", note);
    return 0;
error:
    return upmi_seterror (p, args, "%s: %s", name, pmi_strerror (result));
}

static const struct flux_plugin_handler optab[] = {
    { "upmi.put",           op_put,         NULL },
    { "upmi.get",           op_get,         NULL },
    { "upmi.barrier",       op_barrier,     NULL },
    { "upmi.abort",         op_abort,       NULL },
    { "upmi.initialize",    op_initialize,  NULL },
    { "upmi.finalize",      op_finalize,    NULL },
    { "upmi.preinit",       op_preinit,     NULL },
    { 0 },
};

int upmi_libpmi2_init (flux_plugin_t *p)
{
    if (flux_plugin_register (p, plugin_name, optab) < 0)
        return -1;
    return 0;
}


// vi:ts=4 sw=4 expandtab
