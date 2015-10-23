#ifndef _FLUX_CORE_PMI_SIMPLE_SERVER_H
#define _FLUX_CORE_PMI_SIMPLE_SERVER_H

struct pmi_simple_server;

/* User-provided service implementation.
 * put/get return 0 on success, -1 on failure.
 * barrier returns 0 if incomplete, 1 if complete.
 */
struct pmi_simple_ops {
    int (*kvs_put)(void *arg, const char *kvsname,
                   const char *key, const char *val);
    int (*kvs_get)(void *arg, const char *kvsname,
                   const char *key, char *val, int len);
    int (*barrier)(void *arg);
};

/* Create/destroy protocol engine.
 */
struct pmi_simple_server *pmi_simple_server_create (struct pmi_simple_ops *ops,
                                                    int appnum,
                                                    int universe_size,
                                                    const char *kvsname,
                                                    void *arg);
void pmi_simple_server_destroy (struct pmi_simple_server *pmi);

/* Max buffer size needed to read a null-terminated request line,
 * including trailing newline.
 */
int pmi_simple_server_get_maxrequest (struct pmi_simple_server *pmi);

/* Put null-terminated request with sending client reference to protocol
 * engine.  Caller should remove the trailing newline.
 * Return 0 on success, -1 on failure.
 */
int pmi_simple_server_request (struct pmi_simple_server *pmi,
                               const char *buf, void *client);

/* Get next null-terminated response and destination client reference
 * from protocol engine.  Response is ready to send, trailing newline included.
 * Caller must free response.
 * Return 0 on success, -1 on failure (no more responses).
 */
int pmi_simple_server_response (struct pmi_simple_server *pmi,
                                char **buf, void *client);

#endif /* ! _FLUX_CORE_PMI_SIMPLE_SERVER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
