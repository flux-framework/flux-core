#ifndef _FLUX_KVS_DEPRECATED_H
#define _FLUX_KVS_DEPRECATED_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#include "kvs.h"
#include "src/common/libjson-c/json.h"

typedef int (*kvs_set_obj_f)(const char *key, json_object *val, void *arg,
                             int errnum);

int kvs_get_obj (flux_t *h, const char *key, json_object **valp)
                 __attribute__ ((deprecated));

int kvs_watch_obj (flux_t *h, const char *key, kvs_set_obj_f set, void *arg)
                   __attribute__ ((deprecated));

int kvs_watch_once_obj (flux_t *h, const char *key, json_object **valp)
                        __attribute__ ((deprecated));

int kvs_put_obj (flux_t *h, const char *key, json_object *val)
                 __attribute__ ((deprecated));

int kvsdir_get_obj (kvsdir_t *dir, const char *key, json_object **valp)
                    __attribute__ ((deprecated));

int kvsdir_put_obj (kvsdir_t *dir, const char *key, json_object *val)
                    __attribute__ ((deprecated));

#endif /* !_FLUX_KVS_DEPRECATED_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
