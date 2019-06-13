/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simple_server.c - protocol engine for PMI-1 wire protocol */

/* Users send request lines on behalf of clients to the protocol engine
 * via pmi_simple_server_request().
 *
 * Callbacks are invoked by the protocol engine in response to requests.
 * The callbacks are registered via the operations struct:
 *
 * response_send
 *   Send a response line to client.
 *
 * kvs_put
 *   Put a KVS value.  A success/fail response is generated for the client
 *   upon callback return.
 *
 * kvs_get
 *   Get a KVS value.  No response is generated - it is delayed until
 *   the user calls pmi_simple_server_kvs_get_complete() or _get_error().
 *   Meanwhile the protocol engine can process other clients.
 *
 * barrier_enter (optional)
 *   PMI barriers complete once 'universe_size' procs have entered.  If
 *   local_size == universe_size, the barrier may complete locally and
 *   this callback is unnecessary.  If local_size < universe_size, multiple
 *   instances of the protocol engine must contribute to a count held by
 *   the user to complete the barrier, and this callback is required.
 *   From this callback, call pmi_simple_server_barrier_complete() once the
 *   user-held count reaches 'universe_size'.
 *
 * debug_trace
 *   If flags | PMI_SIMPLE_SERVER_TRACE, this callback will be made
 *   with protocol telemetry for debugging.
 *
 * Notes:
 * - The void *client argument is passed in to pmi_simple_server_request()
 *   by the user and represents a "client handle" of some sort.  It is passed
 *   opaquely through the protocol engine to response_send and other callbacks.
 *
 * - The void *client argument is captured on first use and stored by rank
 *   in pmi->clients.  When exiting a barrier, this hash is iterated over to
 *   generate a response for each client.  When processing the multi-line
 *   spawn command, a client's hash entry hold intermediate parsing state.
 *
 * - This protocol engine is expected to work with line-buffered libsubprocess
 *   "channels", thus the engine I/O is line-oriented.
 *
 * - The following PMI-1 wire protocol commands always return PMI_FAIL:
 *   publish, unpublish, lookup, spawn.
 *
 * - Abort is implemented as a no-op.
 */

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
    int rank;
    void *arg;          // user-supplied void *client argument

    bool mcmd_started;  // client started multi-line spawn command
};

struct pmi_simple_server {
    void *arg;
    struct pmi_simple_ops ops;
    int appnum;
    char *kvsname;
    int universe_size;

    int local_size;
    int local_barrier_count;

    zhashx_t *clients;  // struct client hashed by rank

    int flags;
};


static int pmi_simple_server_kvs_get_error (struct pmi_simple_server *pmi,
                                            void *client, int result);

static struct client *client_create (int rank, void *arg);
static void client_destroy (void **item);
static int client_hash_insert (zhashx_t *zhx, struct client *cli);
static struct client *client_hash_lookup (zhashx_t *zhx, int rank);
static zhashx_t *client_hash_create (void);
static void client_hash_destroy (zhashx_t *zhx);


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
                                                    int local_size,
                                                    const char *kvsname,
                                                    int flags,
                                                    void *arg)
{
    struct pmi_simple_server *pmi = calloc (1, sizeof (*pmi));

    if (!pmi)
        return NULL;
    pmi->ops = ops;
    pmi->arg = arg;
    pmi->appnum = appnum;
    pmi->universe_size = universe_size;
    pmi->local_size = local_size;
    pmi->flags = flags;
    pmi->kvsname = strdup (kvsname);
    pmi->clients = client_hash_create ();
    if (!pmi->kvsname || !pmi->clients) {
        errno = ENOMEM;
        goto error;
    }
    return pmi;
error:
    pmi_simple_server_destroy (pmi);
    return NULL;
}

void pmi_simple_server_destroy (struct pmi_simple_server *pmi)
{
    if (pmi) {
        int saved_errno = errno;
        free (pmi->kvsname);
        client_hash_destroy (pmi->clients);
        free (pmi);
        errno = saved_errno;
    }
}

static int barrier_exit (struct pmi_simple_server *pmi, int rc)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    struct client *cli;
    int ret = 0;

    pmi->local_barrier_count = 0;
    cli = zhashx_first (pmi->clients);
    while (cli) {
        snprintf (resp, sizeof (resp), "cmd=barrier_out rc=%d\n", rc);
        trace (pmi, cli->arg, "S: %s", resp);
        if (pmi->ops.response_send (cli->arg, resp) < 0)
            ret = -1;
        cli = zhashx_next (pmi->clients);
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
                               const char *buf, void *client, int rank)
{
    char resp[SIMPLE_MAX_PROTO_LINE+1];
    int rc = 0;
    struct client *cli;

    if (!(cli = client_hash_lookup (pmi->clients, rank))) {
        if (!(cli = client_create (rank, client)))
            goto error;
        if (client_hash_insert (pmi->clients, cli) < 0) {
            client_destroy ((void **)&cli);
            goto error;
        }
    }

    resp[0] = '\0';
    trace (pmi, client, "C: %s", buf);

    /* spawn continuation (unimplemented) */
    if (cli->mcmd_started) {
        if (strcmp (buf, "endcmd\n") != 0)
            goto out_noresponse; // ignore protocol between mcmd and endcmd
        snprintf (resp, sizeof (resp), "cmd=spawn_result rc=-1\n");
        cli->mcmd_started = false;
    }
    /* init */
    else if (keyval_parse_isword (buf, "cmd", "init") == 0) {
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
            goto out_noresponse;
        result = PMI_ERR_INVALID_KEY;
get_respond:
        return (pmi_simple_server_kvs_get_error (pmi, client, result));
    }
    /* barrier */
    else if (keyval_parse_isword (buf, "cmd", "barrier_in") == 0) {
        if (++pmi->local_barrier_count == pmi->local_size) {
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
        /* FIXME - not implemented */
        cli->mcmd_started = true;
        goto out_noresponse;
    }

    /* unknown command */
    else
        goto proto;
    if (resp[0] != '\0') {
        trace (pmi, client, "S: %s", resp);
        if (pmi->ops.response_send (client, resp) < 0)
            rc = -1;
    }
out_noresponse:
    return rc;
proto:
    errno = EPROTO;
error:
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

static struct client *client_create (int rank, void *arg)
{
    struct client *cli;

    if (!(cli = calloc (1, sizeof (*cli))))
        return NULL;
    cli->rank = rank;
    cli->arg = arg;

    return cli;
}

/* zhashx_destructor_fn footprint */
static void client_destroy (void **item)
{
    free (*item);
    *item = NULL;
}

/* zhashx_hash_fn footprint */
static size_t client_hasher (const void *key)
{
    const int *rank = key;
    return *rank;
}

/* zhashx_comparator_fn footprint */
static int client_hash_key_cmp (const void *key1, const void *key2)
{
    const int *r1 = key1;
    const int *r2 = key2;

    return (*r1 == *r2 ? 0 : (*r1 < *r2 ? -1 : 1));
}

static struct client *client_hash_lookup (zhashx_t *zhx, int rank)
{
    struct client *cli;

    if (!(cli = zhashx_lookup (zhx, &rank))) {
        errno = ENOENT;
        return NULL;
    }
    return cli;
}

static int client_hash_insert (zhashx_t *zhx, struct client *cli)
{
    if (zhashx_insert (zhx, &cli->rank, cli) < 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

static void client_hash_destroy (zhashx_t *zhx)
{
    zhashx_destroy (&zhx);
}

static zhashx_t *client_hash_create (void)
{
    zhashx_t *zhx;

    if (!(zhx = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }

    zhashx_set_destructor (zhx, client_destroy);

    zhashx_set_key_hasher (zhx, client_hasher);
    zhashx_set_key_comparator (zhx, client_hash_key_cmp);
    zhashx_set_key_duplicator (zhx, NULL);
    zhashx_set_key_destructor (zhx, NULL);

    return zhx;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
