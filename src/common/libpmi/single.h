#ifndef _FLUX_CORE_PMI_SINGLE_H
#define _FLUX_CORE_PMI_SINGLE_H

#include "src/common/libpmi/pmi.h"

struct pmi_single;

int pmi_single_init (struct pmi_single *pmi, int *spawned);
int pmi_single_initialized (struct pmi_single *pmi,
                                   int *initialized);
int pmi_single_finalize (struct pmi_single *pmi);
int pmi_single_get_size (struct pmi_single *pmi, int *size);
int pmi_single_get_rank (struct pmi_single *pmi, int *rank);
int pmi_single_get_appnum (struct pmi_single *pmi, int *appnum);
int pmi_single_get_universe_size (struct pmi_single *pmi,
                                         int *universe_size);
int pmi_single_publish_name (struct pmi_single *pmi,
                                    const char *service_name, const char *port);
int pmi_single_unpublish_name (struct pmi_single *pmi,
                                      const char *service_name);
int pmi_single_lookup_name (struct pmi_single *pmi,
                                   const char *service_name, char *port);
int pmi_single_barrier (struct pmi_single *pmi);
int pmi_single_abort (struct pmi_single *pmi,
                             int exit_code, const char *error_msg);
int pmi_single_kvs_get_my_name (struct pmi_single *pmi,
                                       char *kvsname, int length);
int pmi_single_kvs_get_name_length_max (struct pmi_single *pmi,
                                               int *length);
int pmi_single_kvs_get_key_length_max (struct pmi_single *pmi,
                                              int *length);
int pmi_single_kvs_get_value_length_max (struct pmi_single *pmi,
                                                int *length);
int pmi_single_kvs_put (struct pmi_single *pmi,
                               const char *kvsname, const char *key,
                               const char *value);
int pmi_single_kvs_commit (struct pmi_single *pmi,
                                  const char *kvsname);
int pmi_single_kvs_get (struct pmi_single *pmi,
                               const char *kvsname,
                               const char *key, char *value, int len);

int pmi_single_spawn_multiple (struct pmi_single *pmi,
                                      int count,
                                      const char *cmds[],
                                      const char **argvs[],
                                      const int maxprocs[],
                                      const int info_keyval_sizesp[],
                                      const PMI_keyval_t *info_keyval_vectors[],
                                      int preput_keyval_size,
                                      const PMI_keyval_t preput_keyval_vector[],
                                      int errors[]);

void pmi_single_destroy (struct pmi_single *pmi);
struct pmi_single *pmi_single_create (void);


#endif /* _FLUX_CORE_PMI_SINGLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
