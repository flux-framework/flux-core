/* Use PMI to bootstrap a Flux comms session.
 */
typedef struct pmi_struct *pmi_t;

pmi_t pmi_init (const char *libname);
void pmi_fini (pmi_t pmi);

int pmi_rank (pmi_t pmi);
int pmi_size (pmi_t pmi);
const char *pmi_sid (pmi_t pmi);
int pmi_jobid (pmi_t pmi);

int pmi_relay_rank (pmi_t pmi);
int pmi_right_rank (pmi_t);

void pmi_put_uri (pmi_t pmi, int rank, const char *uri);
void pmi_put_relay (pmi_t pmi, int rank, const char *uri);

void pmi_fence (pmi_t pmi);

const char *pmi_get_uri (pmi_t pmi, int rank);
const char *pmi_get_relay (pmi_t pmi, int rank);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
