#ifndef _FLUX_CORE_PMI_CLIENT_H
#define _FLUX_CORE_PMI_CLIENT_H

#ifndef PMI_SUCCESS
#define PMI_SUCCESS                  0
#endif
#ifndef PMI_FAIL
#define PMI_FAIL                    -1
#endif
#ifndef PMI_ERR_INIT
#define PMI_ERR_INIT                 1
#endif
#ifndef PMI_ERR_NOMEM
#define PMI_ERR_NOMEM                2
#endif
#ifndef PMI_ERR_INVALID_ARG
#define PMI_ERR_INVALID_ARG          3
#endif
#ifndef PMI_ERR_INVALID_KEY
#define PMI_ERR_INVALID_KEY          4
#endif
#ifndef PMI_ERR_INVALID_KEY_LENGTH
#define PMI_ERR_INVALID_KEY_LENGTH   5
#endif
#ifndef PMI_ERR_INVALID_VAL
#define PMI_ERR_INVALID_VAL          6
#endif
#ifndef PMI_ERR_INVALID_VAL_LENGTH
#define PMI_ERR_INVALID_VAL_LENGTH   7
#endif
#ifndef PMI_ERR_INVALID_LENGTH
#define PMI_ERR_INVALID_LENGTH       8
#endif
#ifndef PMI_ERR_INVALID_NUM_ARGS
#define PMI_ERR_INVALID_NUM_ARGS     9
#endif
#ifndef PMI_ERR_INVALID_ARGS
#define PMI_ERR_INVALID_ARGS        10
#endif
#ifndef PMI_ERR_INVALID_NUM_PARSED
#define PMI_ERR_INVALID_NUM_PARSED  11
#endif
#ifndef PMI_ERR_INVALID_KEYVALP
#define PMI_ERR_INVALID_KEYVALP     12
#endif
#ifndef PMI_ERR_INVALID_SIZE
#define PMI_ERR_INVALID_SIZE        13
#endif

typedef void (*pmi_free_f)(void *impl);

typedef struct pmi_struct pmi_t;

/* Create/destroy pmi_t class.
 */
void pmi_destroy (pmi_t *pmi);

pmi_t *pmi_create_dlopen (const char *filename);
pmi_t *pmi_create_simple (int fd, int rank, int size);
pmi_t *pmi_create_guess (void);

const char *pmi_strerror (int errnum);


/* Functions below match the canonical PMIv1 ABI, except for
 * the pmi_t first argument and the lower case function names.
 */

typedef struct {
    const char *key;
    char *val;
} pmi_keyval_t;

int pmi_init (pmi_t *pmi, int *spawned);
int pmi_initialized (pmi_t *pmi, int *initialized);
int pmi_finalize (pmi_t *pmi);
int pmi_get_size (pmi_t *pmi, int *size);
int pmi_get_rank (pmi_t *pmi, int *rank);
int pmi_get_universe_size (pmi_t *pmi, int *size);
int pmi_get_appnum (pmi_t *pmi, int *appnum);
int pmi_publish_name (pmi_t *pmi, const char *service_name, const char *port);
int pmi_unpublish_name (pmi_t *pmi, const char *service_name);
int pmi_lookup_name (pmi_t *pmi, const char *service_name, char *port);
int pmi_barrier (pmi_t *pmi);
int pmi_abort (pmi_t *pmi, int exit_code, const char *error_msg);
int pmi_kvs_get_my_name (pmi_t *pmi, char *kvsname, int length);
int pmi_kvs_get_name_length_max (pmi_t *pmi, int *length);
int pmi_kvs_get_key_length_max (pmi_t *pmi, int *length);
int pmi_kvs_get_value_length_max (pmi_t *pmi, int *length);
int pmi_kvs_put (pmi_t *pmi,
        const char *kvsname, const char *key, const char *value);
int pmi_kvs_commit (pmi_t *pmi, const char *kvsname);
int pmi_kvs_get (pmi_t *pmi,
        const char *kvsname, const char *key, char *value, int len);
int pmi_spawn_multiple (pmi_t *pmi,
        int count,
        const char *cmds[],
        const char **argvs[],
        const int maxprocs[],
        const int info_keyval_sizesp[],
        const pmi_keyval_t *info_keyval_vectors[],
        int preput_keyval_size,
        const pmi_keyval_t preput_keyval_vector[],
        int errors[]);

/* These functions were eventually deprecated in MPICH.
 * Be careful using these - if unimplemented PMI_FAIL is returned.
 */

int pmi_get_id (pmi_t *pmi, char *id_str, int length);
int pmi_get_kvs_domain_id (pmi_t *pmi, char *id_str, int length);
int pmi_get_id_length_max (pmi_t *pmi, int *length);
int pmi_get_clique_size (pmi_t *pmi, int *size);
int pmi_get_clique_ranks (pmi_t *pmi, int *ranks, int length);
int pmi_kvs_create (pmi_t *pmi, char *kvsname, int length);
int pmi_kvs_destroy (pmi_t *pmi, const char *kvsname);
int pmi_kvs_iter_first (pmi_t *pmi, const char *kvsname,
        char *key, int key_len, char *val, int val_len);
int pmi_kvs_iter_next (pmi_t *pmi, const char *kvsname,
        char *key, int key_len, char *val, int val_len);
int pmi_parse_option (pmi_t *pmi, int num_args, char *args[],
        int *num_parsed, pmi_keyval_t **keyvalp, int *size);
int pmi_args_to_keyval (pmi_t *pmi, int *argcp, char *((*argvp)[]),
        pmi_keyval_t **keyvalp, int *size);
int pmi_free_keyvals (pmi_t *pmi, pmi_keyval_t keyvalp[], int size);
int pmi_get_options (pmi_t *pmi, char *str, int length);

#endif /* ! _FLUX_CORE_PMI_CLIENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
