/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <unistd.h>
#include <dlfcn.h>
#include <assert.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libpmi/simple_client.h"

#include "pmiutil.h"
#include "liblist.h"

struct pmi_dso {
    void *dso;
    int (*init) (int *spawned);
    int (*finalize) (void);
    int (*get_size) (int *size);
    int (*get_rank) (int *rank);
    int (*get_appnum) (int *appnum);
    int (*barrier) (void);
    int (*kvs_get_my_name) (char *kvsname, int length);
    int (*kvs_put) (const char *kvsname, const char *key, const char *value);
    int (*kvs_commit) (const char *kvsname);
    int (*kvs_get) (const char *kvsname, const char *key, char *value, int len);
};

struct pmi_handle {
    struct pmi_dso *dso;
    struct pmi_simple_client *cli;
    int debug;
};

static void broker_pmi_dlclose (struct pmi_dso *dso)
{
    if (dso) {
        if (dso->dso)
            dlclose (dso->dso);
        free (dso);
    }
}

/* Notes:
 * - Use RTLD_GLOBAL due to issue #432
 */
static struct pmi_dso *broker_pmi_dlopen (const char *pmi_library, int debug)
{
    struct pmi_dso *dso;
    zlist_t *libs = NULL;
    char *name;

    if (!(dso = calloc (1, sizeof (*dso))))
        return NULL;
    if (!pmi_library)
        pmi_library = "libpmi.so";
    if (!(libs = liblist_create (pmi_library)))
        goto error;
    FOREACH_ZLIST (libs, name) {
        dlerror ();
        if (!(dso->dso = dlopen (name, RTLD_NOW | RTLD_GLOBAL))) {
            if (debug) {
                char *errstr = dlerror ();
                if (errstr)
                    log_msg ("%s", errstr);
                else
                    log_msg ("dlopen %s failed", name);
            }
        }
        else if (dlsym (dso->dso, "flux_pmi_library")) {
            if (debug)
                log_msg ("skipping %s", name);
            dlclose (dso->dso);
            dso->dso = NULL;
        }
        else {
            if (debug)
                log_msg ("dlopen %s", name);
        }
    }
    if (!dso->dso)
        goto error;
    dso->init = dlsym (dso->dso, "PMI_Init");
    dso->finalize = dlsym (dso->dso, "PMI_Finalize");
    dso->get_size = dlsym (dso->dso, "PMI_Get_size");
    dso->get_rank = dlsym (dso->dso, "PMI_Get_rank");
    dso->get_appnum = dlsym (dso->dso, "PMI_Get_appnum");
    dso->barrier = dlsym (dso->dso, "PMI_Barrier");
    dso->kvs_get_my_name = dlsym (dso->dso, "PMI_KVS_Get_my_name");
    dso->kvs_put = dlsym (dso->dso, "PMI_KVS_Put");
    dso->kvs_commit = dlsym (dso->dso, "PMI_KVS_Commit");
    dso->kvs_get = dlsym (dso->dso, "PMI_KVS_Get");

    if (!dso->init || !dso->finalize || !dso->get_size || !dso->get_rank
            || !dso->get_appnum || !dso->barrier || !dso->kvs_get_my_name
            || !dso->kvs_put || !dso->kvs_commit || !dso->kvs_get) {
        log_msg ("dlsym: %s is missing required symbols", pmi_library);
        goto error;
    }
    return dso;
error:
    broker_pmi_dlclose (dso);
    zlist_destroy (&libs);
    return NULL;
}

int broker_pmi_kvs_commit (struct pmi_handle *pmi, const char *kvsname)
{
    if (pmi->cli)
        return PMI_SUCCESS;
    if (pmi->dso)
        return pmi->dso->kvs_commit (kvsname);
    return PMI_SUCCESS;
}

int broker_pmi_kvs_put (struct pmi_handle *pmi,
                        const char *kvsname,
                        const char *key,
                        const char *value)
{
    if (pmi->cli)
        return pmi_simple_client_kvs_put (pmi->cli, kvsname, key, value);
    if (pmi->dso)
        return pmi->dso->kvs_put (kvsname, key, value);
    return PMI_SUCCESS;
}

int broker_pmi_kvs_get (struct pmi_handle *pmi,
                               const char *kvsname,
                               const char *key,
                               char *value,
                               int len)
{
    if (pmi->cli)
        return pmi_simple_client_kvs_get (pmi->cli, kvsname, key, value, len);
    if (pmi->dso)
        return pmi->dso->kvs_get (kvsname, key, value, len);
    return PMI_FAIL;
}

int broker_pmi_barrier (struct pmi_handle *pmi)
{
    if (pmi->cli)
        return pmi_simple_client_barrier (pmi->cli);
    if (pmi->dso)
        return pmi->dso->barrier();
    return PMI_SUCCESS;
}

int broker_pmi_get_params (struct pmi_handle *pmi,
                           struct pmi_params *params)
{
    int result;

    if (pmi->cli) {
        params->rank = pmi->cli->rank;
        params->size = pmi->cli->size;
        result = pmi_simple_client_get_appnum (pmi->cli, &params->appnum);
        if (result != PMI_SUCCESS)
            goto error;
        result = pmi_simple_client_kvs_get_my_name (pmi->cli,
                                                    params->kvsname,
                                                    sizeof (params->kvsname));
        if (result != PMI_SUCCESS)
            goto error;
    }
    else if (pmi->dso) {
        result = pmi->dso->get_rank (&params->rank);
        if (result != PMI_SUCCESS)
            goto error;
        result = pmi->dso->get_size (&params->size);
        if (result != PMI_SUCCESS)
            goto error;
        result = pmi->dso->get_appnum (&params->appnum);
        if (result != PMI_SUCCESS)
            goto error;
        result = pmi->dso->kvs_get_my_name (params->kvsname,
                                            sizeof (params->kvsname));
        if (result != PMI_SUCCESS)
            goto error;
    }
    else {
        params->rank = 0;
        params->size = 1;
        params->appnum = 0;
        snprintf (params->kvsname, sizeof (params->kvsname), "singleton");
    }

    return PMI_SUCCESS;
error:
    return result;
}

int broker_pmi_init (struct pmi_handle *pmi)
{
    int spawned;

    if (pmi->cli)
        return pmi_simple_client_init (pmi->cli);
    if (pmi->dso)
        return pmi->dso->init(&spawned);
    return PMI_SUCCESS;
}

int broker_pmi_finalize (struct pmi_handle *pmi)
{
    if (pmi->cli)
        return pmi_simple_client_finalize (pmi->cli);
    if (pmi->dso)
        return pmi->dso->finalize ();
    return PMI_SUCCESS;
}

void broker_pmi_destroy (struct pmi_handle *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        if (pmi->cli)
            pmi_simple_client_destroy (pmi->cli);
        else if (pmi->dso)
            broker_pmi_dlclose (pmi->dso);
        free (pmi);
        errno = saved_errno;
    }
}

/* Attempt to set up PMI-1 wire protocol client.
 * If that fails, try dlopen.
 * If that fails, singleton will be used.
 */
struct pmi_handle *broker_pmi_create (void)
{
    const char *pmi_debug;
    struct pmi_handle *pmi = calloc (1, sizeof (*pmi));
    if (!pmi)
        return NULL;
    pmi_debug = getenv ("FLUX_PMI_DEBUG");
    if (!pmi_debug)
        pmi_debug = getenv ("PMI_DEBUG");
    if (pmi_debug)
        pmi->debug = strtol (pmi_debug, NULL, 10);
    pmi->cli = pmi_simple_client_create_fd (getenv ("PMI_FD"),
                                            getenv ("PMI_RANK"),
                                            getenv ("PMI_SIZE"),
                                            pmi_debug,
                                            NULL);
    /* N.B. SLURM boldly installs its libpmi.so into the system libdir,
     * so it will be found here, even if not running in a SLURM job.
     * Fortunately it emulates singleton in that case, in lieu of failing.
     */
    if (!pmi->cli)
        pmi->dso = broker_pmi_dlopen (getenv ("PMI_LIBRARY"), pmi->debug);
    /* If neither pmi->cli nor pmi->dso is set, singleton is assumed later.
     */
    if (pmi->debug)
        log_msg ("using %s", pmi->cli ? "PMI-1 wire protocol"
                           : pmi->dso ? "dlopen" : "singleton");
    return pmi;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
