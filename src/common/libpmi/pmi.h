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
 *  by the Free Software Foundation; either version 2.1 of the license,
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

/* See Flux RFC 13 */

#ifndef FLUX_PMI_H_INCLUDED
#define FLUX_PMI_H_INCLUDED

#define PMI_SUCCESS                  0
#define PMI_FAIL                    -1
#define PMI_ERR_INIT                 1
#define PMI_ERR_NOMEM                2
#define PMI_ERR_INVALID_ARG          3
#define PMI_ERR_INVALID_KEY          4
#define PMI_ERR_INVALID_KEY_LENGTH   5
#define PMI_ERR_INVALID_VAL          6
#define PMI_ERR_INVALID_VAL_LENGTH   7
#define PMI_ERR_INVALID_LENGTH       8
#define PMI_ERR_INVALID_NUM_ARGS     9
#define PMI_ERR_INVALID_ARGS        10
#define PMI_ERR_INVALID_NUM_PARSED  11
#define PMI_ERR_INVALID_KEYVALP     12
#define PMI_ERR_INVALID_SIZE        13

int PMI_Init (int *spawned);
int PMI_Initialized (int *initialized);
int PMI_Finalize (void);
int PMI_Abort (int exit_code, const char error_msg[]);

int PMI_Get_size (int *size);
int PMI_Get_rank (int *rank);
int PMI_Get_universe_size (int *size);
int PMI_Get_appnum (int *appnum);

int PMI_Get_clique_ranks (int ranks[], int length);
int PMI_Get_clique_size (int *size);

int PMI_KVS_Get_my_name (char kvsname[], int length);

int PMI_KVS_Get_name_length_max (int *length);
int PMI_KVS_Get_key_length_max (int *length);
int PMI_KVS_Get_value_length_max (int *length);

int PMI_KVS_Put (const char kvsname[], const char key[], const char value[]);
int PMI_KVS_Get (const char kvsname[], const char key[],
                  char value[], int length);
int PMI_KVS_Commit (const char kvsname[]);
int PMI_Barrier (void);

int PMI_Publish_name (const char service_name[], const char port[]);
int PMI_Unpublish_name (const char service_name[]);
int PMI_Lookup_name (const char service_name[], char port[]);

typedef struct {
    const char * key;
    char * val;
} PMI_keyval_t;

int PMI_Spawn_multiple(int count,
                       const char * cmds[],
                       const char ** argvs[],
                       const int maxprocs[],
                       const int info_keyval_sizesp[],
                       const PMI_keyval_t * info_keyval_vectors[],
                       int preput_keyval_size,
                       const PMI_keyval_t preput_keyval_vector[],
                       int errors[]);


/* Old API funcs - signatures needed for ABI compliance.
 */
int PMI_Get_id_length_max (int *length);
int PMI_Get_id (char kvsname[], int length);
int PMI_Get_kvs_domain_id (char kvsname[], int length);

int PMI_KVS_Create (char kvsname[], int length);
int PMI_KVS_Destroy (const char kvsname[]);
int PMI_KVS_Iter_first (const char kvsname[], char key[], int key_len,
                        char val[], int val_len);
int PMI_KVS_Iter_next (const char kvsname[], char key[], int key_len,
                       char val[], int val_len);

int PMI_Parse_option (int num_args, char *args[], int *num_parsed,
                      PMI_keyval_t **keyvalp, int *size);
int PMI_Args_to_keyval (int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size);
int PMI_Free_keyvals (PMI_keyval_t keyvalp[], int size);
int PMI_Get_options (char *str, int *length);

#endif /* !FLUX_PMI_H_INCLUDED */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
