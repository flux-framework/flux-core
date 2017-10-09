#ifndef _FLUX_CORE_KVS_WATCH_H
#define _FLUX_CORE_KVS_WATCH_H

#ifdef __cplusplus
extern "C" {
#endif

enum kvs_watch_flags {
    KVS_WATCH_ONCE = 4,
    KVS_WATCH_FIRST = 8,
};

typedef int (*kvs_set_f)(const char *key, const char *json_str, void *arg,
                         int errnum);
typedef int (*kvs_set_dir_f)(const char *key, kvsdir_t *dir, void *arg,
                             int errnum);

/* kvs_watch* is like kvs_get* except the registered callback is called
 * to set the value.  It will be called immediately to set the initial
 * value and again each time the value changes.
 * Any storage associated with the value given the
 * callback is freed when the callback returns.  If a value is unset, the
 * callback gets errnum = ENOENT.
 */
int kvs_watch (flux_t *h, const char *key, kvs_set_f set, void *arg);
int kvs_watch_dir (flux_t *h, kvs_set_dir_f set, void *arg,
                   const char *fmt, ...)
        __attribute__ ((format (printf, 4, 5)));

/* Cancel a kvs_watch, freeing server-side state, and unregistering any
 * callback.  Returns 0 on success, or -1 with errno set on error.
 */
int kvs_unwatch (flux_t *h, const char *key);

/* While the above callback interface makes sense in plugin context,
 * the following is better for API context.  'json_str', 'dirp', and
 * 'valp' are IN/OUT parameters.  You should first read the current
 * value, then pass it into the respective kvs_watch_once call, which
 * will return with a new value when it changes.  (The original value
 * is freed inside the function; the new one must be freed by the
 * caller).  *json_str, *dirp, and *valp may be passed in with a NULL
 * value.  If the key is not set, ENOENT is returned without affecting
 * *valp.
 * FIXME: add more types.
 */
int kvs_watch_once (flux_t *h, const char *key, char **json_str);
int kvs_watch_once_dir (flux_t *h, kvsdir_t **dirp, const char *fmt, ...)
        __attribute__ ((format (printf, 3, 4)));

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_WATCH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
