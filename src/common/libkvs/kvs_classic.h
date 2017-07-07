#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

/* These interfaces are on their way to being deprecated */

int kvs_get (flux_t *h, const char *key, char **json_str);
int kvs_get_dir (flux_t *h, kvsdir_t **dirp, const char *fmt, ...)
            __attribute__ ((format (printf, 3, 4)));

int kvsdir_get (kvsdir_t *dir, const char *key, char **json_str);
int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4)));


#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
