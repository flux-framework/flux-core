#ifndef _FLUX_CORE_KVS_NS_H
#define _FLUX_CORE_KVS_NS_H

#include <flux/core.h>

/* create flags */
enum {
    FLUX_NS_SYNCHRONIZE = 1,    // publish ns.sync on create/remove/update
};

/* lookup flags */
enum {
    FLUX_NS_WAIT = 1,           // wait until created/min_seq reached
};

enum {
    FLUX_NS_SEQ_ANY = 0,        // min_seq "don't care" value
};

flux_future_t *flux_kvs_ns_create (flux_t *h,
                                   uint32_t nodeid,
                                   const char *name,
                                   uint32_t userid,
                                   int flags);

flux_future_t *flux_kvs_ns_remove (flux_t *h,
                                   uint32_t nodeid,
                                   const char *name);

flux_future_t *flux_kvs_ns_lookup (flux_t *h,
                                   uint32_t nodeid,
                                   const char *name,
                                   int min_seq,
                                   int flags);

int flux_kvs_ns_lookup_get (flux_future_t *f,
                            const char **json_str);

int flux_kvs_ns_lookup_getf (flux_future_t *f,
                             const char *fmt, ...);

int flux_kvs_ns_lookup_get_seq (flux_future_t *f,
                                int *seq);

flux_future_t *flux_kvs_ns_commit (flux_t *h,
                                   uint32_t nodeid,
                                   const char *name,
                                   int seq,
                                   const char *json_str);

flux_future_t *flux_kvs_ns_commitf (flux_t *h,
                                    uint32_t nodeid,
                                    const char *name,
                                    int seq,
                                    const char *fmt, ...);

int flux_kvs_ns_event_decode (const flux_msg_t *msg,
                              const char **name,
                              int *seq,
                              const char **json_str);


#endif /* !_FLUX_CORE_KVS_NS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
