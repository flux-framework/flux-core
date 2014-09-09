#ifndef _FLUX_CORE_MODULE_H
#define _FLUX_CORE_MODULE_H

/* Manipulate comms modules.
 * Use rank=-1 for local.
 */
enum {
    FLUX_MOD_FLAGS_MANAGED = 1,
};
int flux_rmmod (flux_t h, int rank, const char *name, int flags);
json_object *flux_lsmod (flux_t h, int rank);
int flux_insmod (flux_t h, int rank, const char *path, int flags,
                 json_object *args);

/* Comms modules must define  MOD_NAME and mod_main().
 */
typedef int (mod_main_f)(flux_t h, zhash_t *args);
extern mod_main_f mod_main;
#define MOD_NAME(x) const char *mod_name = x

#endif /* !FLUX_CORE_MODULE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
