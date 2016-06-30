#ifndef _FLUX_CORE_PMI_SIMPLE_CLIENT_H
#define _FLUX_CORE_PMI_SIMPLE_CLIENT_H

#include "src/common/libpmi/pmi.h"

struct pmi_simple_client;

int pmi_simple_client_init (struct pmi_simple_client *pmi, int *spawned);
int pmi_simple_client_initialized (struct pmi_simple_client *pmi,
                                   int *initialized);
int pmi_simple_client_finalize (struct pmi_simple_client *pmi);
int pmi_simple_client_get_size (struct pmi_simple_client *pmi, int *size);
int pmi_simple_client_get_rank (struct pmi_simple_client *pmi, int *rank);
int pmi_simple_client_get_appnum (struct pmi_simple_client *pmi, int *appnum);
int pmi_simple_client_get_universe_size (struct pmi_simple_client *pmi,
                                         int *universe_size);
int pmi_simple_client_publish_name (struct pmi_simple_client *pmi,
                                    const char *service_name, const char *port);
int pmi_simple_client_unpublish_name (struct pmi_simple_client *pmi,
                                      const char *service_name);
int pmi_simple_client_lookup_name (struct pmi_simple_client *pmi,
                                   const char *service_name, char *port);
int pmi_simple_client_barrier (struct pmi_simple_client *pmi);
int pmi_simple_client_abort (struct pmi_simple_client *pmi,
                             int exit_code, const char *error_msg);
int pmi_simple_client_kvs_get_my_name (struct pmi_simple_client *pmi,
                                       char *kvsname, int length);
int pmi_simple_client_kvs_get_name_length_max (struct pmi_simple_client *pmi,
                                               int *length);
int pmi_simple_client_kvs_get_key_length_max (struct pmi_simple_client *pmi,
                                              int *length);
int pmi_simple_client_kvs_get_value_length_max (struct pmi_simple_client *pmi,
                                                int *length);
int pmi_simple_client_kvs_put (struct pmi_simple_client *pmi,
                               const char *kvsname, const char *key,
                               const char *value);
int pmi_simple_client_kvs_commit (struct pmi_simple_client *pmi,
                                  const char *kvsname);
int pmi_simple_client_kvs_get (struct pmi_simple_client *pmi,
                               const char *kvsname,
                               const char *key, char *value, int len);

int pmi_simple_client_spawn_multiple (struct pmi_simple_client *pmi,
                                      int count,
                                      const char *cmds[],
                                      const char **argvs[],
                                      const int maxprocs[],
                                      const int info_keyval_sizesp[],
                                      const PMI_keyval_t *info_keyval_vectors[],
                                      int preput_keyval_size,
                                      const PMI_keyval_t preput_keyval_vector[],
                                      int errors[]);

void pmi_simple_client_destroy (struct pmi_simple_client *pmi);
struct pmi_simple_client *pmi_simple_client_create (void);


#endif /* _FLUX_CORE_PMI_SIMPLE_CLIENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
