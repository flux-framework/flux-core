/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi/simple_client.h"
#include "src/common/libpmi/dgetline.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libflux/reactor.h"

#include "src/common/libtap/tap.h"

#include "server_thread.h"


int main (int argc, char *argv[])
{
    struct pmi_simple_client *cli;
    struct pmi_server_context *srv;
    int cfd[1];
    int universe_size = -1;
    char *name = NULL, *val = NULL, *val2 = NULL, *val3 = NULL;
    char *key = NULL;
    int rc;
    char pmi_fd[16];
    char pmi_rank[16];
    char pmi_size[16];

    plan (NO_PLAN);

    srv = pmi_server_create (cfd, 1);

    /* create/init
     */
    snprintf (pmi_fd, sizeof (pmi_fd), "%d", cfd[0]);
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);

    ok ((cli = pmi_simple_client_create_fd (pmi_fd, pmi_rank, pmi_size,
                                            NULL, NULL)) != NULL,
        "pmi_simple_client_create OK");
    ok (cli->initialized == false,
        "cli->initialized == false");
    ok (pmi_simple_client_init (cli) == PMI_SUCCESS,
        "pmi_simple_client_init OK");
    ok (cli->spawned == false,
        "cli->spawned == failse");

    /* retrieve basic params
     */
    ok (cli->size == 1,
        "cli->size == 1");
    ok (cli->rank == 0,
        "cli->rank == 0");
    ok (pmi_simple_client_get_universe_size (cli, &universe_size) == PMI_SUCCESS
        && universe_size == cli->size,
        "pmi_simple_client_get_universe_size OK, universe_size=%d", universe_size);
    ok (cli->kvsname_max > 0,
        "cli->kvsname_max > 0");
    ok (cli->keylen_max > 0,
        "cli->keylen_max > 0");
    ok (cli->vallen_max > 0,
        "cli->vallen_max > 0");
    name = xzmalloc (cli->kvsname_max);
    ok (pmi_simple_client_kvs_get_my_name (cli, name,
                                           cli->kvsname_max) == PMI_SUCCESS
        && strlen (name) > 0,
        "pmi_simple_client_kvs_get_my_name OK");
    diag ("kvsname=%s", name);

    /* put foo=bar / commit / barier / get foo
     */
    ok (pmi_simple_client_kvs_put (cli, name, "foo", "bar") == PMI_SUCCESS,
        "pmi_simple_client_kvs_put foo=bar OK");
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK");
    val = xzmalloc (cli->vallen_max);
    ok (pmi_simple_client_kvs_get (cli, name, "foo",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && !strcmp (val, "bar"),
        "pmi_simple_client_kvs_get foo OK, val=%s", val);

    /* put long=... / get long
     */
    val2 = xzmalloc (cli->vallen_max);
    memset (val2, 'x', cli->vallen_max - 1);
    ok (pmi_simple_client_kvs_put (cli, name, "long", val2) == PMI_SUCCESS,
        "pmi_simple_client_kvs_put long=xxx... OK");
    memset (val, 'y', cli->vallen_max); /* not null terminated */
    ok (pmi_simple_client_kvs_get (cli, name, "long",
                                   val, cli->vallen_max) == PMI_SUCCESS
        && strnlen (val2, cli->vallen_max) < cli->vallen_max
        && strcmp (val, val2) == 0,
        "pmi_simple_client_kvs_get long OK, val=xxx...");

    /* put: value too long
     */
    val3 = xzmalloc (cli->vallen_max + 1);
    memset (val3, 'y', cli->vallen_max);
    rc = pmi_simple_client_kvs_put (cli, name, "toolong", val3);
    ok (rc == PMI_ERR_INVALID_VAL_LENGTH,
        "pmi_simple_client_kvs_put val too long fails");

    /* put: key too long
     */
    key = xzmalloc (cli->keylen_max + 1);
    memset (key, 'z', cli->keylen_max);
    rc = pmi_simple_client_kvs_put (cli, name, key, "abc");
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_put key too long fails");

    /* get: key too long
     */
    rc = pmi_simple_client_kvs_get (cli, name, key, val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY_LENGTH,
        "pmi_simple_client_kvs_get key too long fails");

    /* get: no exist
     */
    rc = pmi_simple_client_kvs_get (cli, name, "noexist", val, cli->vallen_max);
    ok (rc == PMI_ERR_INVALID_KEY,
        "pmi_simple_client_kvs_get unknown key fails");

    /* barrier: entry failure
     */
    pmi_set_barrier_entry_failure (srv, 1);
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with entry function failure fails");
    pmi_set_barrier_entry_failure (srv, 0);
    pmi_set_barrier_exit_failure (srv, 1);
    ok (pmi_simple_client_barrier (cli) == PMI_FAIL,
        "pmi_simple_client_barrier with exit function failure fails");
    pmi_set_barrier_exit_failure (srv, 0);
    ok (pmi_simple_client_barrier (cli) == PMI_SUCCESS,
        "pmi_simple_client_barrier OK (rigged errors cleared)");

    /* finalize
     */

    ok (pmi_simple_client_finalize (cli) == PMI_SUCCESS,
        "pmi_simple_client_finalize OK");

    free (name);
    free (val);
    free (val2);
    free (val3);
    free (key);
    pmi_simple_client_destroy (cli);

    pmi_server_destroy (srv);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
