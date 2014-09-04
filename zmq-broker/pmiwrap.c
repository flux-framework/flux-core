/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* pmiwrap.c - wrappers for subset of PMI API
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <pmi.h>

#include "log.h"
#include "util.h"
#include "pmiwrap.h"

struct pmi_struct {
    int (*init)(int *);
    int (*get_size)(int *);
    int (*get_rank)(int *);
    int (*get_appnum)(int *);
    int (*get_id_length_max)(int *);
    int (*get_id)(char *, int);
    int (*get_clique_size)(int *);
    int (*get_clique_ranks)(int *, int);
    int (*kvs_get_my_name)(char *, int);
    int (*kvs_get_name_length_max)(int *);
    int (*kvs_get_key_length_max)(int *);
    int (*kvs_get_value_length_max)(int *);
    int (*kvs_put)(const char *, const char *, const char *);
    int (*kvs_commit)(const char *);
    int (*barrier)(void);
    int (*kvs_get)(const char *, const char *, char *, int);
    int (*abort)(int, const char *);
    int (*finalize)(void);
    void *dso;
    char *id, *kname, *key, *val;
    int *clique, clen, klen, vlen;
};

typedef struct {
    int errnum;
    const char *errstr;
} etab_t;

static etab_t pmi_errors[] = {
    { PMI_SUCCESS,              "operation completed successfully" },
    { PMI_FAIL,                 "operation failed" },
    { PMI_ERR_NOMEM,            "input buffer not large enough" },
    { PMI_ERR_INIT,             "PMI not initialized" },
    { PMI_ERR_INVALID_ARG,      "invalid argument" },
    { PMI_ERR_INVALID_KEY,      "invalid key argument" },
    { PMI_ERR_INVALID_KEY_LENGTH,"invalid key length argument" },
    { PMI_ERR_INVALID_VAL,      "invalid val argument" },
    { PMI_ERR_INVALID_VAL_LENGTH,"invalid val length argument" },
    { PMI_ERR_INVALID_LENGTH,   "invalid length argument" },
    { PMI_ERR_INVALID_NUM_ARGS, "invalid number of arguments" },
    { PMI_ERR_INVALID_ARGS,     "invalid args argument" },
    { PMI_ERR_INVALID_NUM_PARSED, "invalid num_parsed length argument" },
    { PMI_ERR_INVALID_KEYVALP,  "invalid keyvalp argument" },
    { PMI_ERR_INVALID_SIZE,     "invalid size argument" },
};
static const int pmi_errors_len = sizeof (pmi_errors) / sizeof (pmi_errors[0]);

static const char *pmi_strerror (int rc)
{
    static char unknown[] = "pmi error XXXXXXXXX";
    int i;

    for (i = 0; i < pmi_errors_len; i++) {
        if (pmi_errors[i].errnum == rc)
            return pmi_errors[i].errstr;
    }
    snprintf (unknown, sizeof (unknown), "pmi error %d", rc);
    return unknown;
}

void pmi_abort (pmi_t pmi, int rc, const char *fmt, ...)
{
    va_list ap;
    char *s;

    va_start (ap, fmt);
    if (vasprintf (&s, fmt, ap) < 0)
        oom ();
    va_end (ap);
    pmi->abort (rc, s);
    /*NOTREACHED*/
    free (s);
}

pmi_t pmi_init (const char *libname)
{
    pmi_t pmi = xzmalloc (sizeof (*pmi));
    int spawned;

    dlerror ();
    pmi->dso = dlopen (libname, RTLD_NOW | RTLD_GLOBAL);
    if (!pmi->dso || !(pmi->init = dlsym (pmi->dso, "PMI_Init"))
                  || !(pmi->get_size = dlsym (pmi->dso, "PMI_Get_size"))
                  || !(pmi->get_rank = dlsym (pmi->dso, "PMI_Get_rank"))
                  || !(pmi->get_appnum = dlsym (pmi->dso, "PMI_Get_appnum"))
                  || !(pmi->get_id_length_max = dlsym (pmi->dso,
                                                "PMI_Get_id_length_max"))
                  || !(pmi->get_id = dlsym (pmi->dso, "PMI_Get_id"))
                  || !(pmi->get_clique_size = dlsym (pmi->dso,
                                                "PMI_Get_clique_size"))
                  || !(pmi->get_clique_ranks = dlsym (pmi->dso,
                                                "PMI_Get_clique_ranks"))
                  || !(pmi->kvs_get_my_name = dlsym (pmi->dso,
                                                "PMI_KVS_Get_my_name"))
                  || !(pmi->kvs_get_name_length_max = dlsym (pmi->dso,
                                                "PMI_KVS_Get_name_length_max"))
                  || !(pmi->kvs_get_key_length_max = dlsym (pmi->dso,
                                                "PMI_KVS_Get_key_length_max"))
                  || !(pmi->kvs_get_value_length_max = dlsym (pmi->dso,
                                                "PMI_KVS_Get_value_length_max"))
                  || !(pmi->kvs_put = dlsym (pmi->dso, "PMI_KVS_Put"))
                  || !(pmi->kvs_commit = dlsym (pmi->dso, "PMI_KVS_Commit"))
                  || !(pmi->barrier = dlsym (pmi->dso, "PMI_Barrier"))
                  || !(pmi->kvs_get = dlsym (pmi->dso, "PMI_KVS_Get"))
                  || !(pmi->abort = dlsym (pmi->dso, "PMI_Abort"))
                  || !(pmi->finalize = dlsym (pmi->dso, "PMI_Finalize"))) {
        msg_exit ("%s: %s", libname, dlerror ());
    }
    if (pmi->init (&spawned) != PMI_SUCCESS)
        msg_exit ("PMI_Init failed");
    return pmi;
}

void pmi_fini (pmi_t pmi)
{
    int e;
    if ((e = pmi->finalize ()) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Finalize: %s", pmi_strerror (e));
    if (pmi->id)
        free (pmi->id);
    if (pmi->clique)
        free (pmi->clique);
    if (pmi->kname)
        free (pmi->kname);
    if (pmi->key)
        free (pmi->key);
    if (pmi->val)
        free (pmi->val);
    if (pmi->dso)
        dlclose (pmi->dso);
    free (pmi);
}

int pmi_rank (pmi_t pmi)
{
    int e, rank;
    if ((e = pmi->get_rank (&rank)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Get_rank: %s", pmi_strerror (e));
    return rank;
}

int pmi_size (pmi_t pmi)
{
    int e, size;
    if ((e = pmi->get_size (&size)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Get_size: %s", pmi_strerror (e));
    return size;
}

int pmi_clique_size (pmi_t pmi)
{
    int e, size;
    if ((e = pmi->get_clique_size (&size)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Get_clique_size: %s", pmi_strerror (e));
    return size;
}

static int pmi_clique (pmi_t pmi, const int **cliquep)
{
    int e;

    if (!pmi->clique) {
        pmi->clen = pmi_clique_size (pmi);
        pmi->clique = xzmalloc (sizeof (pmi->clique[0]) * pmi->clen);
        if ((e = pmi->get_clique_ranks (pmi->clique, pmi->clen)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_Get_clique_size: %s", pmi_strerror (e));
    }
    *cliquep = pmi->clique;
    return pmi->clen;
}

int pmi_clique_minrank (pmi_t pmi)
{
    int i, min;
    const int *clique;
    int clen = pmi_clique (pmi, &clique);

    for (min = -1, i = 0; i < clen; i++)
        if (min == -1 || clique[i] < min)
            min = clique[i];
    return min;
}

const char *pmi_id (pmi_t pmi)
{
    int e, len;
    if (!pmi->id) {
        if ((e = pmi->get_id_length_max (&len)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_Get_id_length_max: %s", pmi_strerror (e));
        pmi->id = xzmalloc (len);
        if ((e = pmi->get_id (pmi->id, len)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_Get_id: %s", pmi_strerror (e));
    }
    return pmi->id;
}

int pmi_appnum (pmi_t pmi)
{
    int e, appnum;
    if ((e = pmi->get_appnum (&appnum)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Get_appnum: %s", pmi_strerror (e));
    return appnum;
}

static const char *pmi_kname (pmi_t pmi)
{
    int e, len;
    if (!pmi->kname) {
        if ((e = pmi->kvs_get_name_length_max (&len)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_KVS_Get_name_length_max: %s",
                       pmi_strerror (e));
        pmi->kname = xzmalloc (len);
        if ((e = pmi->kvs_get_my_name (pmi->kname, len)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_KVS_Get_my_name: %s", pmi_strerror (e));
    }
    return pmi->kname;
}

static int pmi_valbuf (pmi_t pmi, char **vp)
{
    int e;
    if (!pmi->val) {
        if ((e = pmi->kvs_get_value_length_max (&pmi->vlen)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_KVS_Get_value_length_max: %s",pmi_strerror (e));
        pmi->val = xzmalloc (pmi->vlen);
    }
    *vp = pmi->val;
    return pmi->vlen;
}

static int pmi_keybuf (pmi_t pmi, char **kp)
{
    int e;

    if (!pmi->key) {
        if ((e = pmi->kvs_get_key_length_max (&pmi->klen)) != PMI_SUCCESS)
            pmi_abort (pmi, 1, "PMI_KVS_Get_key_length_max: %s",
                       pmi_strerror (e));
        pmi->key = xzmalloc (pmi->klen);
    }
    *kp = pmi->key;
    return pmi->klen;
}

void pmi_kvs_put (pmi_t pmi, const char *val, const char *fmt, ...)
{
    const char *kname = pmi_kname (pmi);
    char *key;
    int klen = pmi_keybuf (pmi, &key);
    va_list ap;
    int e;

    va_start (ap, fmt);
    if (vsnprintf (key, klen, fmt, ap) >= klen)
        pmi_abort (pmi, 1, "%s: key longer than %d", __FUNCTION__, klen);
    va_end (ap);
    if ((e = pmi->kvs_put (kname, key, val)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_KVS_Put %s=%s: %s", key, val, pmi_strerror (e));
}

const char *pmi_kvs_get (pmi_t pmi, const char *fmt, ...)
{
    const char *kname = pmi_kname (pmi);
    char *key, *val;
    int klen = pmi_keybuf (pmi, &key);
    int vlen = pmi_valbuf (pmi, &val);
    va_list ap;
    int e;

    va_start (ap, fmt);
    if (vsnprintf (key, klen, fmt, ap) >= klen)
        pmi_abort (pmi, 1, "%s: key longer than %d", __FUNCTION__, klen);
    va_end (ap);
    if ((e = pmi->kvs_get (kname, key, val, vlen)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_KVS_Get %s: %s", key, pmi_strerror (e));

    return val;
}

void pmi_kvs_fence (pmi_t pmi)
{
    const char *kname = pmi_kname (pmi);
    int e;

    if ((e = pmi->kvs_commit (kname)) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_KVS_Commit: %s", pmi_strerror (e));
    if ((e = pmi->barrier ()) != PMI_SUCCESS)
        pmi_abort (pmi, 1, "PMI_Barrier: %s", pmi_strerror (e));
}

/* Get IP address to use for communication.
 * FIXME: add option to override this via commandline, e.g. --iface=eth0
 */
void pmi_getip (pmi_t pmi, char *ipaddr, int len)
{
    char hostname[HOST_NAME_MAX + 1];
    struct addrinfo hints, *res = NULL;
    int e;

    if (gethostname (hostname, sizeof (hostname)) < 0)
        pmi_abort (pmi, 1, "gethostname: %s", strerror (errno));
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((e = getaddrinfo (hostname, NULL, &hints, &res)) || res == NULL)
        pmi_abort (pmi, 1, "getaddrinfo %s: %s", hostname, gai_strerror (e));
    if ((e = getnameinfo (res->ai_addr, res->ai_addrlen, ipaddr, len,
                          NULL, 0, NI_NUMERICHOST)))
        pmi_abort (pmi, 1, "getnameinfo %s: %s", hostname, gai_strerror (e));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
