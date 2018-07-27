/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <czmq.h>

#include "simple_server.h"
#include "keyval.h"
#include "pmi.h"


struct client {
    zlist_t *mcmd;
};

struct pmi_simple_server {
    void *arg;
    struct pmi_simple_ops ops;
    int appnum;
    char *kvsname;
    int universe_size;
    int local_procs;
    zlist_t *barrier;
    zhash_t *clients;
    int flags;
};


static int pmi_simple_server_kvs_get_error (struct pmi_simple_server *pmi,
                                            void *client, int result);


static void trace (struct pmi_simple_server *pmi,
                   void *client, const char *fmt, ...)
{
    va_list ap;

    if ((pmi->flags & PMI_SIMPLE_SERVER_TRACE)) {
        char buf[SIMPLE_MAX_PROTO_LINE];
        va_start (ap, fmt);
        (void)vsnprintf (buf, sizeof (buf), fmt, ap);
        va_end (ap);
        if (pmi->ops.debug_trace)
            pmi->ops.debug_trace (client, buf);
    }
}

struct pmi_simple_server *pmi_simple_server_create (struct pmi_simple_ops ops,
                                                    int appnum,
                                                    int universe_size,
                                                    int local_procs,
                                                    const char *kvsname,
                                                    int flags,
                                                    void *arg)
{
    struct pmi_simple_server *pmi = calloc (1, sizeof (*pmi));
    int saved_errno;

    if (!pmi) {
        errno = ENOMEM;
        goto error;
    }
    pmi->ops = ops;
    pmi->arg = arg;
    pmi->appnum = appnum;
    if (!(pmi->kvsname = strdup (kvsname))) {
        errno = ENOMEM;
        goto error;
    }
    pmi->universe_size = universe_size;
    pmi->local_procs = local_procs;
    if (!(pmi->barrier = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    pmi->flags = flags;
    return pmi;
error:
    saved_errno = errno;
    pmi_simple_server_destroy (pmi);
    errno = saved_errno;
    return NULL;
}

void pmi_simple_server_destroy (struct pmi_simple_server *pmi)
{
    if (pmi) {
        if (pmi->kvsname)
            free (pmi->kvsname);
        zlist_destroy (&pmi->barrier);
        zhash_destroy (&pmi->clients);
        free (pmi);
    }
}

static void client_destroy (void *arg)
{
    struct client *c = arg;

    if (c) {
        if (c->mcmd) {
            char *cpy;
            while ((cpy = zlist_pop (c->mcmd)))
                free (cpy);
            zlist_destroy (&c->mcmd);
        }
        free (c);
    }
}

static int mcmd_execute (struct pmi_simple_server *pmi, void *client,
                         struct client *c)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    char *buf = zlist_first (c->mcmd);
    int rc = 0;

    resp[0] = '\0';
    if (keyval_parse_isword (buf, "mcmd", "spawn") == 0) {
        /* FIXME - spawn not implemented */
        snprintf (resp, sizeof (resp), "cmd=spawn_result rc=-1\n");
    }
    /* unknown mcmd */
    else {
        errno = EPROTO;
        rc = -1;
    }
    if (resp[0] != '\0') {
        trace (pmi, client, "S: %s", resp);
        if (pmi->ops.response_send (client, resp) < 0)
            rc = -1;
    }
    return rc;
}

static int mcmd_begin (struct pmi_simple_server *pmi, void *client,
                       const char *buf)
{
    struct client *c;
    char ptrkey[2*sizeof (void *) + 1];
    char *cpy = NULL;

    if (!pmi->clients && !(pmi->clients = zhash_new ())) {
        errno = ENOMEM;
        return -1;
    }
    snprintf (ptrkey, sizeof (ptrkey), "%p", client);
    if ((c = zhash_lookup (pmi->clients, ptrkey)) != NULL)
        return -1; /* in progress */
    if (!(c = calloc (1, sizeof (*c)))) {
        errno = ENOMEM;
        return -1;
    }
    if (!(c->mcmd = zlist_new ())) {
        free (c);
        errno = ENOMEM;
        return -1;
    }
    zhash_update (pmi->clients, ptrkey, c);
    zhash_freefn (pmi->clients, ptrkey, client_destroy);
    if (!(cpy = strdup (buf)) || zlist_append (c->mcmd, cpy) < 0) {
        if (cpy)
            free (cpy);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int mcmd_inprogress (struct pmi_simple_server *pmi, void *client)
{
    char ptrkey[2*sizeof (void *) + 1];

    snprintf (ptrkey, sizeof (ptrkey), "%p", client);
    if (!pmi->clients || !zhash_lookup (pmi->clients, ptrkey))
        return 0;
    return 1;
}

static int mcmd_append (struct pmi_simple_server *pmi, void *client,
                        const char *buf)
{
    struct client *c;
    char ptrkey[2*sizeof (void *) + 1];
    char *cpy = NULL;
    int rc = 0;

    snprintf (ptrkey, sizeof (ptrkey), "%p", client);
    if (!pmi->clients || !(c = zhash_lookup (pmi->clients, ptrkey))) {
        errno = EPROTO;
        return -1; /* not in progress */
    }
    if (!(cpy = strdup (buf)) || zlist_append (c->mcmd, cpy) < 0) {
        if (cpy)
            free (cpy);
        errno = ENOMEM;
        return -1;
    }
    if (!strcmp (buf, "endcmd\n")) {
        rc = mcmd_execute (pmi, client, c);
        zhash_delete (pmi->clients, ptrkey);
    }
    return rc;
}

static int barrier_enter (struct pmi_simple_server *pmi, void *client)
{
    if (zlist_append (pmi->barrier, client) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int barrier_exit (struct pmi_simple_server *pmi, int rc)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    void *client;
    int ret = 0;

    while ((client = zlist_pop (pmi->barrier))) {
        snprintf (resp, sizeof (resp), "cmd=barrier_out rc=%d\n", rc);
        trace (pmi, client, "S: %s", resp);
        if (pmi->ops.response_send (client, resp) < 0)
            ret = -1;
    }
    return ret;
}

static int client_respond (struct pmi_simple_server *pmi, void *client,
                           const char *resp)
{
    if (resp[0] != '\0') {
        trace (pmi, client, "S: %s", resp);
        if (pmi->ops.response_send (client, resp) < 0)
            return (-1);
    }
    return (0);
}

int pmi_simple_server_request (struct pmi_simple_server *pmi,
                               const char *buf, void *client)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    int rc = 0;

    resp[0] = '\0';
    trace (pmi, client, "C: %s", buf);

    /* continue in-progress mcmd */
    if (mcmd_inprogress (pmi, client)) {
        rc = mcmd_append (pmi, client, buf);
        goto done;
    }

    /* init */
    if (keyval_parse_isword (buf, "cmd", "init") == 0) {
        unsigned int pmi_version, pmi_subversion;
        if (keyval_parse_uint (buf, "pmi_version", &pmi_version) < 0)
            goto proto;
        if (keyval_parse_uint (buf, "pmi_subversion", &pmi_subversion) < 0)
            goto proto;
        if (pmi_version < 1 || (pmi_version == 1 && pmi_subversion < 1))
            snprintf (resp, sizeof (resp), "cmd=response_to_init rc=-1\n");
        else
            snprintf (resp, sizeof (resp), "cmd=response_to_init rc=0 "
                      "pmi_version=1 pmi_subversion=1\n");
    }
    /* maxes */
    else if (keyval_parse_isword (buf, "cmd", "get_maxes") == 0) {
        snprintf (resp, sizeof (resp), "cmd=maxes rc=0 "
                  "kvsname_max=%d keylen_max=%d vallen_max=%d\n",
                  SIMPLE_KVS_NAME_MAX, SIMPLE_KVS_KEY_MAX, SIMPLE_KVS_VAL_MAX);
    }
    /* abort */
    else if (keyval_parse_isword (buf, "cmd", "abort") == 0) {
        /* FIXME - terminate program */
        snprintf (resp, sizeof (resp), "\n");
    }
    /* finalize */
    else if (keyval_parse_isword (buf, "cmd", "finalize") == 0) {
        snprintf (resp, sizeof (resp), "cmd=finalize_ack rc=0\n");
        rc = 1; /* Indicates fd should be closed */
    }
    /* universe */
    else if (keyval_parse_isword (buf, "cmd", "get_universe_size") == 0) {
        snprintf (resp, sizeof (resp), "cmd=universe_size rc=0 size=%d\n",
                  pmi->universe_size);
    }
    /* appnum */
    else if (keyval_parse_isword (buf, "cmd", "get_appnum") == 0) {
        snprintf (resp, sizeof (resp), "cmd=appnum rc=0 appnum=%d\n",
                  pmi->appnum);
    }
    /* kvsname */
    else if (keyval_parse_isword (buf, "cmd", "get_my_kvsname") == 0) {
        snprintf (resp, sizeof (resp), "cmd=my_kvsname rc=0 kvsname=%s\n",
                  pmi->kvsname);
    }
    /* put */
    else if (keyval_parse_isword (buf, "cmd", "put") == 0) {
        char name[SIMPLE_KVS_NAME_MAX];
        char key[SIMPLE_KVS_KEY_MAX];
        char val[SIMPLE_KVS_VAL_MAX];
        int result = keyval_parse_word (buf, "kvsname", name, sizeof (name));
        if (result < 0) {
            if (result == EKV_VAL_LEN) {
                result = PMI_ERR_INVALID_LENGTH;
                goto put_respond;
            }
            goto proto;
        }
        result = keyval_parse_word (buf, "key", key, sizeof (key));
        if (result < 0) {
            if (result == EKV_VAL_LEN) {
                result = PMI_ERR_INVALID_KEY_LENGTH;
                goto put_respond;
            }
            goto proto;
        }
        result = keyval_parse_string (buf, "value", val, sizeof (val));
        if (result < 0) {
            if (result == EKV_VAL_LEN) {
                result = PMI_ERR_INVALID_VAL_LENGTH;
                goto put_respond;
            }
            goto proto;
        }
        if (pmi->ops.kvs_put (pmi->arg, name, key, val) < 0)
            result = PMI_ERR_INVALID_KEY;
put_respond:
        snprintf (resp, sizeof (resp), "cmd=put_result rc=%d\n", result);
    }
    /* get */
    else if (keyval_parse_isword (buf, "cmd", "get") == 0) {
        char name[SIMPLE_KVS_NAME_MAX];
        char key[SIMPLE_KVS_KEY_MAX];
        int result = keyval_parse_word (buf, "kvsname", name, sizeof (name));
        if (result < 0) {
            if (result == EKV_VAL_LEN) {
                result = PMI_ERR_INVALID_LENGTH;
                goto get_respond;
            }
            goto proto;
        }
        result = keyval_parse_word (buf, "key", key, sizeof (key));
        if (result < 0) {
            if (result == EKV_VAL_LEN) {
                result = PMI_ERR_INVALID_KEY_LENGTH;
                goto get_respond;
            }
            goto proto;
        }
        if (pmi->ops.kvs_get (pmi->arg, client, name, key) == 0)
            goto done;
        result = PMI_ERR_INVALID_KEY;
get_respond:
        return (pmi_simple_server_kvs_get_error (pmi, client, result));
    }
    /* barrier */
    else if (keyval_parse_isword (buf, "cmd", "barrier_in") == 0) {
        if (barrier_enter (pmi, client) < 0)
            rc = -1;
        else if (zlist_size (pmi->barrier) == pmi->local_procs) {
            if (pmi->ops.barrier_enter) {
                if (pmi->ops.barrier_enter (pmi->arg) < 0)
                    if (barrier_exit (pmi, PMI_FAIL) < 0)
                        rc = -1;
            } else
                if (barrier_exit (pmi, 0) < 0)
                    rc = -1;
        }
    }
    /* publish */
    else if (keyval_parse_isword (buf, "cmd", "publish_name") == 0) {
        /* FIXME - not implemented */
        snprintf (resp, sizeof (resp), "cmd=publish_result rc=-1 msg=%s\n",
                  "command not implemented");
    }
    /* unpublish */
    else if (keyval_parse_isword (buf, "cmd", "unpublish_name") == 0) {
        /* FIXME - not implemented */
        snprintf (resp, sizeof (resp), "cmd=unpublish_result rc=-1 msg=%s\n",
                  "command not implemented");
    }
    /* lookup */
    else if (keyval_parse_isword (buf, "cmd", "lookup_name") == 0) {
        /* FIXME - not implemented */
        snprintf (resp, sizeof (resp), "cmd=lookup_result rc=-1 msg=%s\n",
                  "command not implemented");
    }
    /* spawn */
    else if (keyval_parse_isword (buf, "mcmd", "spawn") == 0) {
        rc = mcmd_begin (pmi, client, buf);
    }
    /* unknown command */
    else
        goto proto;
    if (resp[0] != '\0') {
        trace (pmi, client, "S: %s", resp);
        if (pmi->ops.response_send (client, resp) < 0)
            rc = -1;
    }
done:
    return rc;
proto:
    errno = EPROTO;
    return -1;
}

int pmi_simple_server_barrier_complete (struct pmi_simple_server *pmi, int rc)
{
    return barrier_exit (pmi, rc);
}

static int pmi_simple_server_kvs_get_error (struct pmi_simple_server *pmi,
                                            void *client, int result)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    snprintf (resp, sizeof (resp), "cmd=get_result rc=%d\n", result);
    return (client_respond (pmi, client, resp));
}

int pmi_simple_server_kvs_get_complete (struct pmi_simple_server *pmi,
                                        void *client, const char *val)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    if (val == NULL)
        return (pmi_simple_server_kvs_get_error (pmi, client,
                                                 PMI_ERR_INVALID_KEY));
    snprintf (resp, sizeof (resp), "cmd=get_result rc=0 value=%s\n", val);
    return (client_respond (pmi, client, resp));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
