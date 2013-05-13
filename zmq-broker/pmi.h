/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef PMI_H_INCLUDED
#define PMI_H_INCLUDED

#ifdef USE_PMI2_API
#error This header file defines the PMI v1 API, but PMI2 was selected
#endif

/**
 ** canonical PMI-1 interfaces
 **/

#if defined(__cplusplus)
extern "C" {
#endif

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

int PMI_Init( int *spawned );
int PMI_Initialized( int *initialized );
int PMI_Finalize( void );

int PMI_Get_size( int *size );			/* SLURM_NTASKS */
int PMI_Get_rank( int *rank );			/* SLURM_PROCID */
int PMI_Get_universe_size( int *size );		/* SLURM_NTASKS */
int PMI_Get_appnum( int *appnum );		/* SLURM_JOB_ID */

/* PMI_{Publish,Unpublish,Lookup}_name not implemented here or in SLURM's PMI */
int PMI_Publish_name( const char service_name[], const char port[] );
int PMI_Unpublish_name( const char service_name[] );
int PMI_Lookup_name( const char service_name[], char port[] );

int PMI_Barrier( void );

int PMI_Abort(int exit_code, const char error_msg[]);

int PMI_KVS_Get_my_name( char kvsname[], int length );
int PMI_KVS_Get_name_length_max( int *length );
int PMI_KVS_Get_key_length_max( int *length );
int PMI_KVS_Get_value_length_max( int *length );
int PMI_KVS_Put( const char kvsname[], const char key[], const char value[]);
int PMI_KVS_Commit( const char kvsname[] );
int PMI_KVS_Get( const char kvsname[], const char key[], char value[], int length);

typedef struct PMI_keyval_t
{
    const char * key;
    char * val;
} PMI_keyval_t;

/* PMI_Spawn_multiple not implemented here or in SLURM's PMI */
int PMI_Spawn_multiple(int count,
                       const char * cmds[],
                       const char ** argvs[],
                       const int maxprocs[],
                       const int info_keyval_sizesp[],
                       const PMI_keyval_t * info_keyval_vectors[],
                       int preput_keyval_size,
                       const PMI_keyval_t preput_keyval_vector[],
                       int errors[]);

/**
 ** extended PMI-1 interfaces
 **  (these are also provided by SLURM's PMI implementation)
 **/

/* id is SLURM_JOB_ID . SLURM_STEP_ID */
int PMI_Get_id( char id_str[], int length );
int PMI_Get_kvs_domain_id( char id_str[], int length );	/* openmpi */
int PMI_Get_id_length_max( int *length );		/* openmpi */

/* clique_ranks/size derived from SLURM_GTIDS */
int PMI_Get_clique_size( int *size );			/* openmpi */
int PMI_Get_clique_ranks( int ranks[], int length);	/* openpmi */

/* everything below here not implemented */
int PMI_KVS_Create( char kvsname[], int length );
int PMI_KVS_Destroy( const char kvsname[] );
int PMI_KVS_Iter_first(const char kvsname[], char key[], int key_len,
                        char val[], int val_len);
int PMI_KVS_Iter_next(const char kvsname[], char key[], int key_len,
                        char val[], int val_len);

/* keyval/options functions not implemented */
int PMI_Parse_option(int num_args, char *args[], int *num_parsed,
                        PMI_keyval_t **keyvalp, int *size);
int PMI_Args_to_keyval(int *argcp, char *((*argvp)[]),
                        PMI_keyval_t **keyvalp, int *size);
int PMI_Free_keyvals(PMI_keyval_t keyvalp[], int size);
int PMI_Get_options(char *str, int *length);


#if defined(__cplusplus)
}
#endif

#endif
