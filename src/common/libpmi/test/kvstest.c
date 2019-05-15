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
#include "src/common/libutil/monotime.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"

#define OPTIONS "nN:l:"
static const struct option longopts[] = {
    {"n-squared", no_argument, 0, 'n'},
    {"key-count", required_argument, 0, 'N'},
    {"library", required_argument, 0, 'l'},
    {0, 0, 0, 0},
};

int main (int argc, char *argv[])
{
    struct timespec t;
    int rank, size;
    int e, spawned, initialized, kvsname_len, key_len, val_len;
    char *kvsname, *key, *val, *val2;
    bool nsquared = false;
    int ch;
    int i, j, keycount = 1;
    char *library = NULL;

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'n': /* --n-squared */
                nsquared = true;
                break;
            case 'N': /* --key-count N */
                keycount = strtoul (optarg, NULL, 10);
                break;
            case 'l': /* --library */
                library = optarg;
                break;
        }
    }

    /* Initial handshake with PMI obtains
     *    rank, size, and some string max lengths
     */
    if (library) {
        unsetenv ("PMI_FD");
        setenv ("PMI_LIBRARY", library, 1);
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
    e = PMI_KVS_Get_name_length_max (&kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_name_length_max: %s", rank, pmi_strerror (e));
    e = PMI_KVS_Get_key_length_max (&key_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_key_length_max: %s", rank, pmi_strerror (e));
    e = PMI_KVS_Get_value_length_max (&val_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_value_length_max: %s", rank, pmi_strerror (e));

    kvsname = xzmalloc (kvsname_len);
    key = xzmalloc (key_len);
    val = xzmalloc (val_len);
    val2 = xzmalloc (val_len);

    e = PMI_KVS_Get_my_name (kvsname, kvsname_len);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Get_my_name: %s", rank, pmi_strerror (e));

    /* Put phase
     * (keycount * PUT) + COMMIT + BARRIER
     */
    monotime (&t);
    for (i = 0; i < keycount; i++) {
        snprintf (key, key_len, "kvstest-%d-%d", rank, i);
        snprintf (val, val_len, "sandwich.%d.%d", rank, i);
        e = PMI_KVS_Put (kvsname, key, val);
        if (e != PMI_SUCCESS)
            log_msg_exit ("%d: PMI_KVS_Put: %s", rank, pmi_strerror (e));
    }
    e = PMI_KVS_Commit (kvsname);
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_KVS_Commit: %s", rank, pmi_strerror (e));
    e = PMI_Barrier ();
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Barrier: %s", rank, pmi_strerror (e));
    if (rank == 0)
        printf ("%d: put phase: %.3f msec\n", rank, monotime_since (t));

    /* Get phase
     * no options:    (keycount * GET) + BARRIER
     * --n-squared:   (keycount * GET * size) + BARRIER
     */
    monotime (&t);
    for (i = 0; i < keycount; i++) {
        if (nsquared) {
            for (j = 0; j < size; j++) {
                snprintf (key, key_len, "kvstest-%d-%d", j, i);
                e = PMI_KVS_Get (kvsname, key, val, val_len);
                if (e != PMI_SUCCESS)
                    log_msg_exit ("%d: PMI_KVS_Get: %s", rank, pmi_strerror (e));
                snprintf (val2, val_len, "sandwich.%d.%d", j, i);
                if (strcmp (val, val2) != 0)
                    log_msg_exit ("%d: PMI_KVS_Get: exp %s got %s\n", rank, val2, val);
            }
        } else {
            snprintf (key, key_len, "kvstest-%d-%d", rank > 0 ? rank - 1 : size - 1, i);
            e = PMI_KVS_Get (kvsname, key, val, val_len);
            if (e != PMI_SUCCESS)
                log_msg_exit ("%d: PMI_IVS_Get: %s", rank, pmi_strerror (e));
            snprintf (val2,
                      val_len,
                      "sandwich.%d.%d",
                      rank > 0 ? rank - 1 : size - 1,
                      i);
            if (strcmp (val, val2) != 0)
                log_msg_exit ("%d: PMI_KVS_Get: exp %s got %s\n", rank, val2, val);
        }
    }
    e = PMI_Barrier ();
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Barrier: %s", rank, pmi_strerror (e));
    if (rank == 0)
        printf ("%d: get phase: %.3f msec\n", rank, monotime_since (t));

    e = PMI_Finalize ();
    if (e != PMI_SUCCESS)
        log_msg_exit ("%d: PMI_Finalize: %s", rank, pmi_strerror (e));

    free (val);
    free (val2);
    free (key);
    free (kvsname);

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
