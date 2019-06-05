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
    struct pmi_server_context *srv;
    int cfd[1];
    int universe_size;
    char *kvsname;
    char *val;
    char pmi_fd[16];
    char pmi_rank[16];
    char pmi_size[16];
    int result;
    int spawned;
    int initialized;
    int size;
    int appnum;
    int rank;
    int kvsname_max;
    int keylen_max;
    int vallen_max;
    int n;
    char buf[64];
    int clique_size;
    int clique_ranks[1];

    plan (NO_PLAN);

    srv = pmi_server_create (cfd, 1);

    snprintf (pmi_fd, sizeof (pmi_fd), "%d", cfd[0]);
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);

    setenv ("PMI_FD", pmi_fd, 1);
    setenv ("PMI_RANK", pmi_rank, 1);
    setenv ("PMI_SIZE", pmi_size, 1);

    setenv ("PMI_DEBUG", "1", 1);
    setenv ("PMI_SPAWNED", "0", 1);

    /* Elicit PMI_ERR_INIT error by calling functions before PMI_Init()
     */
    result = PMI_Initialized (&initialized);
    ok (result == PMI_SUCCESS && initialized == 0,
        "PMI_Initialized() works and set initialized=0");

    result = PMI_Finalize ();
    ok (result == PMI_ERR_INIT,
        "PMI_Finalize before init fails with PMI_ERR_INIT");

    result = PMI_Get_size (&size);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_size before init fails with PMI_ERR_INIT");

    result = PMI_Get_rank (&rank);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_rank before init fails with PMI_ERR_INIT");

    result = PMI_Get_universe_size (&universe_size);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_universe_size before init fails with PMI_ERR_INIT");

    result = PMI_Get_appnum (&appnum);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_appnum before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Get_name_length_max (&kvsname_max);
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Get_name_length_max before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Get_key_length_max (&keylen_max);
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Get_key_length_max before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Get_value_length_max (&vallen_max);
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Get_value_length_max before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Get_my_name (buf, sizeof (buf));
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Get_my_name before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Put ("foo", "bar", "baz");
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Put before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Commit ("foo");
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Commit before init fails with PMI_ERR_INIT");

    result = PMI_Barrier ();
    ok (result == PMI_ERR_INIT,
        "PMI_Barrier before init fails with PMI_ERR_INIT");

    result = PMI_KVS_Get ("foo", "bar", buf, sizeof (buf));
    ok (result == PMI_ERR_INIT,
        "PMI_KVS_Get before init fails with PMI_ERR_INIT");

    result = PMI_Get_clique_size (&clique_size);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_clique_size before init fails with PMI_ERR_INIT");

    result = PMI_Get_clique_ranks (clique_ranks, 1);
    ok (result == PMI_ERR_INIT,
        "PMI_Get_clique_ranks before init fails with PMI_ERR_INIT");

    /* Initialize
     */
    result = PMI_Init (&spawned);
    ok (result == PMI_SUCCESS && spawned == 0,
        "PMI_Init works and set spawned=0");

    result = PMI_Initialized (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Initialized with NULL arg fails with PMI_ERR_INVALID_ARG");

    result = PMI_Initialized (&initialized);
    ok (result == PMI_SUCCESS && initialized == 1,
        "PMI_Initialized works and set initialized=1");

    /* second init */
    result = PMI_Init (&spawned);
    ok (result == PMI_ERR_INIT,
        "Second PMI_Init fails with PMI_ERR_INIT");

    /* retrieve basic params
     */
    result = PMI_Get_size (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_size with NULL arg fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_size (&size);
    ok (result == PMI_SUCCESS && size == 1,
        "PMI_Get_size works and set size=1");

    result = PMI_Get_rank (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_rank with NULL arg fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_rank (&rank);
    ok (result == PMI_SUCCESS && rank == 0,
        "PMI_Get_rank works and set rank=0");

    result = PMI_Get_universe_size (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_universe_size with NULL arg fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_universe_size (&universe_size);
    ok (result == PMI_SUCCESS && universe_size == 1,
        "PMI_Get_universe_size works and set universe_size=1");

    result = PMI_Get_appnum (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_appnum with NULL arg fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_appnum (&appnum);
    ok (result == PMI_SUCCESS && appnum == 42,
        "PMI_Get_appnum works and set appnum=42");

    /* retrieve maxes
     */
    result = PMI_KVS_Get_name_length_max (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get_name_length_max len=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get_name_length_max (&kvsname_max);
    ok (result == PMI_SUCCESS && kvsname_max > 0,
        "PMI_KVS_Get_KVS_Get_name_length_max works and returned value > 0");

    result = PMI_KVS_Get_key_length_max (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get_key_length_max len=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get_key_length_max (&keylen_max);
    ok (result == PMI_SUCCESS && keylen_max > 0,
        "PMI_KVS_Get_KVS_Get_key_length_max works and returned value > 0");

    result = PMI_KVS_Get_value_length_max (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get_value_length_max len=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get_value_length_max (&vallen_max);
    ok (result == PMI_SUCCESS && vallen_max > 0,
        "PMI_Get_KVS_Get_value_length_max works and returned value > 0");

    val = xzmalloc (vallen_max);

    /* get kvsname
     */
    kvsname = xzmalloc (kvsname_max);

    result = PMI_KVS_Get_my_name (NULL, kvsname_max);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get_my_name kvsname=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get_my_name (kvsname, -1);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get_my_name len=-1 fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get_my_name (kvsname, kvsname_max);
    ok (result == PMI_SUCCESS,
        "PMI_Get_KVS_Get_my_name works");
    diag ("kvsname=%s", kvsname);

    /* put foo=bar / commit / barier / get foo
     */
    result = PMI_KVS_Put (NULL, "foo", "bar");
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Put kvsname=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Put (kvsname, NULL, "bar");
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Put key=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Put (kvsname, "foo", NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Put val=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Put (kvsname, "foo", "bar");
    ok (result == PMI_SUCCESS,
        "PMI_KVS_Put works");

    result = PMI_KVS_Commit (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Commit kvsname=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Commit (kvsname);
    ok (result == PMI_SUCCESS,
        "PMI_KVS_Commit works");

    result = PMI_Barrier ();
    ok (result == PMI_SUCCESS,
        "PMI_Barrier works");

    result = PMI_KVS_Get (NULL, "foo", val, vallen_max);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get kvsname=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get (kvsname, NULL, val, vallen_max);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get key=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get (kvsname, "foo", NULL, vallen_max);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get val=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get (kvsname, "foo", val, -1);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_KVS_Get length=-1 fails with PMI_ERR_INVALID_ARG");

    result = PMI_KVS_Get (kvsname, "foo", val, vallen_max);
    ok (result == PMI_SUCCESS && !strcmp (val, "bar"),
        "PMI_KVS_Get works and got expected value");

    /* clique
     */
    result = PMI_Get_clique_size (NULL);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_clique_size size=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_clique_ranks (NULL, 1);
    ok (result == PMI_ERR_INVALID_ARG,
        "PMI_Get_clique_ranks ranks=NULL fails with PMI_ERR_INVALID_ARG");

    result = PMI_Get_clique_ranks (clique_ranks, 0);
    ok (result == PMI_ERR_INVALID_SIZE,
        "PMI_Get_clique_ranks size=0 fails with PMI_ERR_INVALID_SIZE");

    result = PMI_Get_clique_size (&clique_size);
    ok (result == PMI_SUCCESS && clique_size == 1,
        "PMI_Get_clique_size works and set size = 1");

    result = PMI_Get_clique_ranks (clique_ranks, 1);
    ok (result == PMI_SUCCESS && clique_ranks[0] == 0,
        "PMI_Get_clique_ranks works and set ranks[0] = 0");

    result = PMI_KVS_Put (kvsname, "PMI_process_mapping", "(vector,(0,1,1))");
    ok (result == PMI_SUCCESS,
        "successfully stored PMI_process_mapping");

    result = PMI_Get_clique_size (&clique_size);
    ok (result == PMI_SUCCESS && clique_size == 1,
        "PMI_Get_clique_size retrieved expected clique size");

    result = PMI_Get_clique_ranks (clique_ranks, 1);
    ok (result == PMI_SUCCESS && clique_ranks[0] == 0,
        "PMI_Get_clique_ranks retrieved expected clique ranks");

    /* not implemented
     */
    result = PMI_Publish_name ("foo", "42");
    ok (result == PMI_FAIL,
        "PMI_Publish_name (unimplemented) returns PMI_FAIL");

    result = PMI_Unpublish_name ("foo");
    ok (result == PMI_FAIL,
        "PMI_Unpublish_name (unimplemented) returns PMI_FAIL");

    result = PMI_Lookup_name ("foo", "42");
    ok (result == PMI_FAIL,
        "PMI_Lookup_name (unimplemented) returns PMI_FAIL");

    result = PMI_Spawn_multiple (0,     // count
                                 NULL,  // cmds
                                 NULL,  // argvs
                                 NULL,  // maxprocs
                                 NULL,  // info_keyval_sizesp
                                 NULL,  // info_keyval_vectors
                                 0,     // preput_keyval_size
                                 NULL,  // preput_keyval_vector
                                 NULL); // errors
    ok (result == PMI_FAIL,
        "PMI_Spawn_multiple (unimplemented) returns PMI_FAIL");

    result = PMI_KVS_Create (buf, sizeof (buf));
    ok (result == PMI_FAIL,
        "PMI_KVS_Create (unimplemented) resturns PMI_FAIL");

    result = PMI_KVS_Destroy ("foo");
    ok (result == PMI_FAIL,
        "PMI_KVS_Destroy (unimplemented) resturns PMI_FAIL");

    result = PMI_KVS_Iter_first ("foo", buf, sizeof (buf), buf, sizeof (buf));
    ok (result == PMI_FAIL,
        "PMI_KVS_Iter_first (unimplemented) resturns PMI_FAIL");

    result = PMI_KVS_Iter_next ("foo", buf, sizeof (buf), buf, sizeof (buf));
    ok (result == PMI_FAIL,
        "PMI_KVS_Iter_next (unimplemented) resturns PMI_FAIL");

    result = PMI_Parse_option (0, NULL, NULL, NULL, NULL);
    ok (result == PMI_FAIL,
        "PMI_Parse_option (unimplemented) resturns PMI_FAIL");

    result = PMI_Args_to_keyval (NULL, NULL, NULL, NULL);
    ok (result == PMI_FAIL,
        "PMI_Args_to_keyval (unimplemented) resturns PMI_FAIL");

    result = PMI_Free_keyvals (NULL, 0);
    ok (result == PMI_FAIL,
        "PMI_Free_keyvals (unimplemented) resturns PMI_FAIL");

    result = PMI_Get_options (NULL, NULL);
    ok (result == PMI_FAIL,
        "PMI_Get_options (unimplemented) resturns PMI_FAIL");


    /* aliases
     */
    result = PMI_Get_id_length_max (&n);
    ok (result == PMI_SUCCESS && n == kvsname_max,
        "PMI_Get_id_lenght_max works and set idlen to kvsname_max");

    result = PMI_Get_id (buf, sizeof (buf));
    ok (result == PMI_SUCCESS && !strcmp (buf, kvsname),
        "PMI_Get_id works and set buf to kvsname");

    result = PMI_Get_kvs_domain_id (buf, sizeof (buf));
    ok (result == PMI_SUCCESS && !strcmp (buf, kvsname),
        "PMI_Get_kvs_domain_id works and set buf to kvsname");

    /* finalize
     */
    result = PMI_Finalize ();
    ok (result == PMI_SUCCESS,
        "PMI_Finalize works");

    free (kvsname);
    free (val);

    pmi_server_destroy (srv);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
