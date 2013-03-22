#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "pmi.h"

#ifndef PMI_FALSE
#define PMI_FALSE 0
#endif
#ifndef PMI_TRUE
#define PMI_TRUE 1
#endif

static void errx (char *msg, int rc)
{
    fprintf (stderr, "%s: %s\n", msg,
        rc == PMI_SUCCESS ? "success" :
        rc == PMI_FAIL ? "fail" :
        rc == PMI_ERR_INIT ? "err_init" :
        rc == PMI_ERR_INVALID_ARG ? "err_invalid_arg" :
        rc == PMI_ERR_INVALID_KEY ? "err_invalid_key" :
        rc == PMI_ERR_INVALID_KEY_LENGTH ? "err_invalid_key_length" :
        rc == PMI_ERR_INVALID_VAL ? "err_invalid_val" :
        rc == PMI_ERR_INVALID_VAL_LENGTH ? "err_invalid_val_length" :
        rc == PMI_ERR_INVALID_NUM_ARGS ? "invalid_num_args" :
        rc == PMI_ERR_INVALID_ARGS ? "invalid_args" :
        rc == PMI_ERR_INVALID_NUM_PARSED? "invalid_num_parsed" :
        rc == PMI_ERR_INVALID_KEYVALP ? "invalid_keyvalp" :
        rc == PMI_ERR_INVALID_SIZE ? "invalid_size" : "UNKNOWN ERROR");
    exit (1);
}

int main (int argc, char **argv)
{
    int rc;
    int spawned = -1;
    int initialized = -1;
    int length = 0;
    char *kvsname;
    char val[16];

    /* initialize */

    rc = PMI_Initialized (&initialized);
    if (rc != PMI_SUCCESS)
        errx ("PMI_Initialized", rc);
    assert (initialized == PMI_FALSE);

    rc = PMI_Init (&spawned);
    if (rc != PMI_SUCCESS)
        errx ("PMI_Init", rc);

    rc = PMI_Initialized (&initialized);
    if (rc != PMI_SUCCESS)
        errx ("PMI_Initialized", rc);
    assert (initialized == PMI_TRUE);

    /* get kvsname */

    rc = PMI_KVS_Get_name_length_max (&length);
    if (rc != PMI_SUCCESS)
        errx ("PMI_KVS_Get_name_length_max", rc);
    kvsname = malloc (length);
    if (kvsname == NULL) {
        fprintf (stderr, "out of memory\n");
        exit (1);
    }

    rc = PMI_KVS_Get_my_name (kvsname, length);
    if (rc != PMI_SUCCESS)
        errx ("PMI_KVS_Get_my_name", rc);
    printf ("kvsname = %s\n", kvsname);
    free (kvsname);

    /* put a key-val */

    rc = PMI_KVS_Put (kvsname, "answer", "rhubarb pie");
    if (rc != PMI_SUCCESS)
        errx ("PMI_KVS_Put", rc);
    printf ("stored answer=rhubarb pie\n");

    /* get it back */

    rc = PMI_KVS_Get (kvsname, "answer", val, sizeof (val));
    if (rc != PMI_SUCCESS)
        errx ("PMI_KVS_Get", rc);
    printf ("retrieved answer=%s\n", val);

    /* try to get an unknown key */

    rc = PMI_KVS_Get (kvsname, "foo", val, sizeof (val));
    if (rc != PMI_SUCCESS && rc != PMI_FAIL)
        errx ("PMI_KVS_Get", rc);
    printf ("retrieved foo=%s\n", rc == PMI_SUCCESS ? val : "<undefined>");

    /* finalize */

    rc = PMI_Finalize ();
    if (rc != PMI_SUCCESS)
        errx ("PMI_Finalize", rc);

    rc = PMI_Initialized (&initialized);
    if (rc != PMI_SUCCESS)
        errx ("PMI_Initialized", rc);
    assert (initialized == PMI_FALSE);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
