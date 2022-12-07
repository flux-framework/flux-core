/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdbool.h>
#include <getopt.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"

#define OPTIONS "ca:"
static const struct option longopts[] = {
    {"clique",       no_argument,        0, 'c'},
    {"abort",        required_argument,  0, 'a'},
    {0, 0, 0, 0},
};

static char *pmi_cliquetostr (char *buf, int bufsz, int *ranks, int length)
{
    int n, i, count;

    buf[0] = '\0';
    for (i = 0, count  = 0; i < length; i++) {
        n = snprintf (buf + count,
                      bufsz - count,
                      "%s%d",
                      i > 0 ? "," : "",
                      ranks[i]);
        if (n >= bufsz - count)
            return "overflow";
        count += n;
    }
    return buf;
}

int main(int argc, char *argv[])
{
    int rank, size, appnum;
    int e, spawned, initialized, kvsname_len, key_len, val_len;
    int universe_size;
    char *kvsname;
    int ch;
    int copt = 0;
    int abort = -1;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':   /* --clique */
                copt++;
                break;
            case 'a':   /* --abort */
                abort = atoi (optarg);
                break;
        }
    }
    e = PMI_Init (&spawned);
    if (e != PMI_SUCCESS)
        log_msg_exit ("PMI_Init: %s", pmi_strerror (e));
    e = PMI_Initialized (&initialized);
    if (e != PMI_SUCCESS)
        log_msg_exit ("PMI_Initialized: %s", pmi_strerror (e));
    if (initialized == 0)
        log_msg_exit ("PMI_Initialized says nope!");
    e = PMI_Get_rank (&rank);
    if (e != PMI_SUCCESS)
        log_msg_exit ("PMI_Get_rank: %s", pmi_strerror (e));
    e = PMI_Get_size (&size);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Get_size: %s", rank, pmi_strerror (e));
    e = PMI_Get_universe_size (&universe_size);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Get_universe_size: %s", rank, pmi_strerror (e));
    e = PMI_KVS_Get_name_length_max (&kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_name_length_max: %s",
                      rank, pmi_strerror (e));
    e = PMI_KVS_Get_key_length_max (&key_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_key_length_max: %s",
                      rank, pmi_strerror (e));
    e = PMI_KVS_Get_value_length_max (&val_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_value_length_max: %s",
                      rank, pmi_strerror (e));
    kvsname = xzmalloc (kvsname_len);
    e = PMI_KVS_Get_my_name (kvsname, kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_my_name: %s", rank, pmi_strerror (e));


    /* Display clique info
     * If that fails, show the PMI_process_mapping.
     */
    if (copt) {
        int clen;
        int *clique;
        char *s;
        char buf[4096];

        e = PMI_Get_clique_size (&clen);
        if (e != PMI_SUCCESS)
            log_msg_exit ("%d PMI_Get_clique_size: %s",
                          rank, pmi_strerror (e));
        clique = xzmalloc (sizeof (clique[0]) * clen);
        e = PMI_Get_clique_ranks (clique, clen);
        if (e != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_Get_clique_ranks: %s",
                          rank, pmi_strerror(e));
        s = pmi_cliquetostr (buf, sizeof (buf), clique, clen);
        printf ("%d: clique=%s\n", rank, s);
        free (clique);
    }
    /* Generic info
     */
    else {

        e = PMI_Get_appnum (&appnum);
        if (e != PMI_SUCCESS)
            log_msg_exit ("PMI_Get_appnum: %s", pmi_strerror (e));

        printf ("%d: size=%d appnum=%d maxes=%d:%d:%d kvsname=%s\n",
                rank, size, appnum, kvsname_len, key_len, val_len, kvsname);
    }

    if (abort == rank)
        PMI_Abort (1, "Test abort error. ok. yeah!");

    e = PMI_Finalize ();
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Finalize: %s", rank, pmi_strerror (e));

    free (kvsname);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
