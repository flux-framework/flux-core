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
#include "src/common/libpmi/pmi2.h"
#include "src/common/libflux/reactor.h"

#include "src/common/libtap/tap.h"

#include "server_thread.h"


int main (int argc, char *argv[])
{
    struct pmi_server_context *srv;
    int cfd[1];
    char jobid[PMI2_MAX_ATTRVALUE + 1];
    char val[PMI2_MAX_VALLEN + 1];
    char longkey[2*PMI2_MAX_KEYLEN];
    char pmi_rank[16];
    char pmi_size[16];
    int result;
    int spawned;
    int size;
    int appnum;
    int rank;
    char buf[64];
    int vallen;
    int found;

    plan (NO_PLAN);

    /* Modify environment before spawning server thread to avoid
     *  racing with getenv() in libev during reactor initialization
     */
    snprintf (pmi_rank, sizeof (pmi_rank), "%d", 0);
    snprintf (pmi_size, sizeof (pmi_size), "%d", 1);

    setenv ("PMI_RANK", pmi_rank, 1);
    setenv ("PMI_SIZE", pmi_size, 1);

    setenv ("PMI2_DEBUG", "1", 1);
    setenv ("PMI_SPAWNED", "0", 1);

    /* PMI_FD exported in pmi_server_create */
    srv = pmi_server_create (cfd, 1);

    /* Elicit PMI2_ERR_INIT error by calling functions before PMI_Init()
     */
    ok (PMI2_Initialized () == 0,
        "PMI2_Initialized() returns 0");

    result = PMI2_Finalize ();
    ok (result == PMI2_ERR_INIT,
        "PMI2_Finalize before init fails with PMI2_ERR_INIT");

    result = PMI2_Job_GetId (jobid, sizeof (jobid));
    ok (result == PMI2_ERR_INIT,
        "PMI2_Job_GetId before init fails with PMI2_ERR_INIT");

    result = PMI2_KVS_Put ("foo", "bar");
    ok (result == PMI2_ERR_INIT,
        "PMI2_KVS_Put before init fails with PMI2_ERR_INIT");

    result = PMI2_KVS_Fence ();
    ok (result == PMI2_ERR_INIT,
        "PMI2_KVS_Fence before init fails with PMI2_ERR_INIT");

    result = PMI2_KVS_Get ("foo", 0, "bar", buf, sizeof (buf), &vallen);
    ok (result == PMI2_ERR_INIT,
        "PMI2_KVS_Get before init fails with PMI2_ERR_INIT");

    /* Initialize
     */
    result = PMI2_Init (&spawned, &size, &rank, &appnum);
    ok (result == PMI2_SUCCESS
        && spawned == 0
        && size == 1
        && rank == 0
        && appnum == 42,
        "PMI2_Init works and set spawned=0 size=1 rank=0 appnum=42");

    ok (PMI2_Initialized () != 0,
        "PMI2_Initialized returns nonzero");

    /* second init */
    result = PMI2_Init (&spawned, &size, &rank, &appnum);
    ok (result == PMI2_ERR_INIT,
        "Second PMI2_Init fails with PMI_ERR2_INIT");

    /* Get job attributes
     */
    found = 1;
    result = PMI2_Info_GetJobAttr (NULL, val, sizeof (val), &found);
    ok (result == PMI2_ERR_INVALID_ARG && found == 0,
       "PMI2_Info_GetJobAttr name=NULL fails with PMI2_ERR_INVALID_ARG and found=0");

    found = 1;
    result = PMI2_Info_GetJobAttr ("universeSize", NULL, 0, &found);
    ok (result == PMI2_ERR_INVALID_ARG && found == 0,
       "PMI2_Info_GetJobAttr val=NULL fails with PMI2_ERR_INVALID_ARG and found=0");

    found = 1;
    result = PMI2_Info_GetJobAttr ("unknownKey", val, sizeof (val), &found);
    ok (result == PMI2_ERR_INVALID_KEY && found == 0,
       "PMI2_Info_GetJobAttr name=unknownKey fails with PMI2_ERR_INVALID_KEY and found=0");

    found = 0;
    val[0] = '\0';
    result = PMI2_Info_GetJobAttr ("universeSize",
                                   val,
                                   sizeof (val),
                                   &found);
    ok (result == PMI2_SUCCESS && found != 0 && !strcmp (val, "1"),
       "PMI2_Info_GetJobAttr PMI_process_mapping works and found != 0");

    jobid[0] = '\0';
    result = PMI2_Job_GetId (jobid, sizeof (jobid));
    ok (result == PMI2_SUCCESS
        && !strcmp (jobid, "bleepgorp"),
        "PMI2_Job_GetId works");

    /* Exchange node scope data
     */
    result = PMI2_Info_PutNodeAttr ("attr1", "xyz");
    ok (result == PMI2_SUCCESS,
        "PMI2_Info_PutNodeAttr name=attr1 works");

    found = 42;
    result = PMI2_Info_GetNodeAttr ("attr1", val, sizeof (val), &found, 0);
    ok (result == PMI2_SUCCESS
        && found == 1
        && strcmp (val, "xyz") == 0,
        "PMI2_Info_GetNodeAttr name=attr1 works");

    found = 42;
    result = PMI2_Info_GetNodeAttr ("attr1", val, sizeof (val), &found, 1);
    ok (result == PMI2_SUCCESS
        && found == 1
        && strcmp (val, "xyz") == 0,
        "PMI2_Info_GetNodeAttr name=attr1 waitfor=1 works");

    found = 42;
    result = PMI2_Info_GetNodeAttr ("noexist", val, sizeof (val), &found, 0);
    ok (result == PMI2_SUCCESS
        && found == 0,
        "PMI2_Info_GetNodeAttr name=noexist returns PMI2_SUCCESS with found=0");

    result = PMI2_Info_GetNodeAttr ("noexist", val, sizeof (val), NULL, 0);
    ok (result == PMI2_ERR_INVALID_KEY,
        "PMI2_Info_GetNodeAttr name=noexist found=NULL returns"
        " PMI2_ERR_INVALID_KEY");

    memset (longkey, 'a', sizeof (longkey));
    longkey[sizeof (longkey) - 1] = '\0';

    result = PMI2_Info_GetNodeAttr (NULL, val, sizeof (val), NULL, 0);
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_Info_GetNodeAttr name=NULL returns PMI2_ERR_INVALID_ARG");
    result = PMI2_Info_GetNodeAttr ("attr1", NULL, 0, NULL, 0);
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_Info_GetNodeAttr value=NULL returns PMI2_ERR_INVALID_ARG");
    result = PMI2_Info_GetNodeAttr (longkey, val, sizeof (val), NULL, 0);
    ok (result == PMI2_ERR_INVALID_KEY_LENGTH,
        "PMI2_Info_GetNodeAttr name=longkey returns PMI2_ERR_INVALID_KEY_LENGTH");

    result = PMI2_Info_PutNodeAttr (NULL, "xyz");
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_Info_PutNodeAttr name=NULL returns PMI2_ERR_INVALID_ARG");
    result = PMI2_Info_PutNodeAttr ("attr2", NULL);
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_Info_PutNodeAttr value=NULL returns PMI2_ERR_INVALID_ARG");
    result = PMI2_Info_PutNodeAttr (longkey, "xyz");
    ok (result == PMI2_ERR_INVALID_KEY_LENGTH,
        "PMI2_Info_PutNodeAttr name=longkey returns PMI2_ERR_INVALID_KEY_LENGTH");

    /* put foo=bar / fence / get foo
     */
    result = PMI2_KVS_Put (NULL, "bar");
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_KVS_Put key=NULL fails with PMI2_ERR_INVALID_ARG");

    result = PMI2_KVS_Put ("foo", NULL);
    ok (result == PMI2_ERR_INVALID_ARG,
        "PMI2_KVS_Put val=NULL fails with PMI2_ERR_INVALID_ARG");

    result = PMI2_KVS_Put ("foo", "bar");
    ok (result == PMI2_SUCCESS,
        "PMI2_KVS_Put works");

    result = PMI2_KVS_Fence();
    ok (result == PMI2_SUCCESS,
        "PMI2_KVS_Fence works");

    result = PMI2_KVS_Get (NULL, 0, "foo", val, sizeof (val), &vallen);
    ok (result == PMI2_SUCCESS,
        "PMI2_KVS_Get jobid=NULL works");

    result = PMI2_KVS_Get (jobid, 0, "foo", val, sizeof (val), &vallen);
    ok (result == PMI2_SUCCESS
        && !strcmp (val, "bar")
        && vallen == strlen (val),
        "PMI2_KVS_Get works and got expected value");

    /* not implemented
     */
    result = PMI2_Job_GetRank (NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Job_GetRank (unimplemented) returns PMI2_FAIL");

    result = PMI2_Job_Connect (NULL, NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Job_Connect (unimplemented) returns PMI2_FAIL");

    result = PMI2_Job_Disconnect (NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Job_Disconnect (unimplemented) returns PMI2_FAIL");

    result = PMI2_Info_GetSize (NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Info_GetSize (unimplemented) returns PMI2_FAIL");

    result = PMI2_Info_GetNodeAttrIntArray (NULL, NULL, 0, NULL, NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Info_GetNodeAttrIntArray (unimplemented) returns PMI2_FAIL");

    result = PMI2_Info_GetJobAttrIntArray (NULL, NULL, 0, NULL, NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Info_GetJobAttrIntArray (unimplemented) returns PMI2_FAIL");

    result = PMI2_Nameserv_publish (NULL, NULL, NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Nameserv_publish (unimplemented) returns PMI2_FAIL");

    result = PMI2_Nameserv_lookup (NULL, NULL, NULL, 0);
    ok (result == PMI2_FAIL,
        "PMI2_Nameserv_lookup (unimplemented) returns PMI2_FAIL");

    result = PMI2_Nameserv_unpublish (NULL, NULL);
    ok (result == PMI2_FAIL,
        "PMI2_Nameserv_unpublish (unimplemented) returns PMI2_FAIL");

    result = PMI2_Job_Spawn (0,     // count
                             NULL,  // cmds
                             NULL,  // argcs
                             NULL,  // argvs
                             NULL,  // maxprocs
                             NULL,  // info_keyval_sizesp
                             NULL,  // info_keyval_vectors
                             0,     // preput_keyval_size
                             NULL,  // preput_keyval_vector
                             NULL,  // jobId
                             0,     // jobIdSize
                             NULL); // errors
    ok (result == PMI2_FAIL,
        "PMI2_Job_Spawn (unimplemented) returns PMI2_FAIL");

    /* finalize
     */
    result = PMI2_Finalize ();
    ok (result == PMI2_SUCCESS,
        "PMI2_Finalize works");

    result = PMI2_Finalize ();
    ok (result == PMI2_ERR_INIT,
        "second PMI2_Finalize fails with PMI2_ERR_INIT");

    pmi_server_destroy (srv);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
