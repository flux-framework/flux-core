#ifndef _FLUX_CORE_PMI_CLIENT_IMPL_H
#define _FLUX_CORE_PMI_CLIENT_IMPL_H

struct pmi_struct {
    int (*init)(void *impl, int *spawned);
    int (*initialized)(void *impl, int *initialized);
    int (*finalize)(void *impl); int (*get_size)(void *impl, int *size);
    int (*get_rank)(void *impl, int *rank);
    int (*get_universe_size)(void *impl, int *size);
    int (*get_appnum)(void *impl, int *appnum);
    int (*publish_name)(void *impl, const char *service_name, const char *port);
    int (*unpublish_name)(void *impl, const char *service_name);
    int (*lookup_name)(void *impl, const char *service_name, char *port);
    int (*barrier)(void *impl); int (*abort)(void *impl,
            int exit_code, const char *error_msg);
    int (*kvs_get_my_name)(void *impl, char *kvsname, int length);
    int (*kvs_get_name_length_max)(void *impl, int *length);
    int (*kvs_get_key_length_max)(void *impl, int *length);
    int (*kvs_get_value_length_max)(void *impl, int *length);
    int (*kvs_put)(void *impl,
            const char *kvsname, const char *key, const char *value);
    int (*kvs_commit)(void *impl, const char *kvsname);
    int (*kvs_get)(void *impl,
            const char *kvsname, const char *key, char *value, int len);
    int (*spawn_multiple)(void *impl,
            int count,
            const char *cmds[],
            const char **argvs[],
            const int maxprocs[],
            const int info_keyval_sizesp[],
            const pmi_keyval_t *info_keyval_vectors[],
            int preput_keyval_size,
            const pmi_keyval_t preput_keyval_vector[],
            int errors[]);

    // deprecated
    int (*get_id)(void *impl, char *id_str, int length);
    int (*get_kvs_domain_id)(void *impl, char *id_str, int length);
    int (*get_id_length_max)(void *impl, int *length);
    int (*get_clique_size)(void *impl, int *size);
    int (*get_clique_ranks)(void *impl, int *ranks, int length);
    int (*kvs_create)(void *impl, char *kvsname, int length);
    int (*kvs_destroy)(void *impl, const char *kvsname);
    int (*kvs_iter_first)(void *impl, const char *kvsname,
            char *key, int key_len, char *val, int val_len);
    int (*kvs_iter_next)(void *impl, const char *kvsname,
            char *key, int key_len, char *val, int val_len);
    int (*parse_option)(void *impl, int num_args, char *args[], int *num_parsed,
            pmi_keyval_t **keyvalp, int *size);
    int (*args_to_keyval)(void *impl, int *argcp, char *((*argvp)[]),
            pmi_keyval_t **keyvalp, int *size);
    int (*free_keyvals)(void *impl, pmi_keyval_t keyvalp[], int size);
    int (*get_options)(void *impl, char *str, int length);

    // internal
    void *impl;
    pmi_free_f impl_destroy;
};

pmi_t *pmi_create (void *impl, pmi_free_f destroy);

#endif /* ! _FLUX_CORE_PMI_CLIENT_IMPL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
