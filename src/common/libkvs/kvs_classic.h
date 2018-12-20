/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef _FLUX_KVS_CLASSIC_H
#define _FLUX_KVS_CLASSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* These functions are from an earlier era and should not be used in
 * new code.
 */

/* flux_kvs_get() and flux_kvs_get_dir() combine a flux_kvs_lookup()
 * and flux_kvs_lookup_get() call in one function, thus they block
 * the calling thread while RPC's complete, and duplicate the returned
 * result leaving it up to the caller to free.
 *
 * flux_kvs_get_dir() includes printf-style (fmt, ...) varargs for
 * constructing the key argument on the fly.
 */

int flux_kvs_get (flux_t *h, const char *key, char **json_str)
                    __attribute__ ((deprecated));
int flux_kvs_get_dir (flux_t *h, flux_kvsdir_t **dirp, const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4), deprecated));

/* These functions are like their counterparts in "kvs_txn.h" except
 * they append operations to an "anonymous" transaction.
 * Use flux_kvs_commit_anon() or flux_kvs_fence_anon() to commit the
 * anonymous transaction.  Generally it is more clear to use an explicit
 * transaction.
 */

int flux_kvs_put (flux_t *h, const char *key, const char *json_str)
                    __attribute__ ((deprecated));
int flux_kvs_unlink (flux_t *h, const char *key)
                    __attribute__ ((deprecated));
int flux_kvs_symlink (flux_t *h, const char *key, const char *target)
                    __attribute__ ((deprecated));
int flux_kvs_mkdir (flux_t *h, const char *key)
                    __attribute__ ((deprecated));

/* flux_kvs_commit_anon() and flux_kvs_fence_anon() combine a flux_kvs_commit()
 * and a flux_future_get() call in one function, thus they block the calling
 * thread while RPC's complete.  These functions operate only on the
 * anonymous transaction (see above).
 */

int flux_kvs_commit_anon (flux_t *h, int flags)
                    __attribute__ ((deprecated));
int flux_kvs_fence_anon (flux_t *h, const char *name, int nprocs, int flags)
                    __attribute__ ((deprecated));

/* flux_kvsdir_get() and flux_kvsdir_get_dir() combine a flux_kvs_lookup()
 * and flux_kvs_get() call in one function, thus they block the calling
 * thread while RPC's complete, and duplicate the returned result leaving
 * it up to the caller to free.
 *
 * The flux_kvsdir_t object acts a container for the flux_t handle used
 * to fetch it; a root snapshot reference, if originally fetched with
 * flux_future_getat(); and the key used to fetch it.  The key supplied
 * to flux_kvsdir_get() or flux_kvsdir_get_dir() is combined with the original
 * key to construct a new key that is passed to flux_kvs_lookup().
 *
 * If the flux_kvsdir_t contains a root snapshot reference, flux_kvsdir_getat()
 * is used to fetch the new value, thus the new values are relative to the
 * snapshot, not the changing root.
 *
 * flux_kvsdir_get_dir() includes printf-style (fmt, ...) varargs for
 * constructing the key argument on the fly.
 */

int flux_kvsdir_get (const flux_kvsdir_t *dir,
                    const char *key, char **json_str)
                    __attribute__ ((deprecated));
int flux_kvsdir_get_dir (const flux_kvsdir_t *dir, flux_kvsdir_t **dirp,
                    const char *fmt, ...)
                    __attribute__ ((format (printf, 3, 4), deprecated));

/* These functions are like their counterparts in "kvs_txn.h" except
 * they append operations to an "anonymous" transaction, and construct
 * the key as a subdirectory of 'dir' as described above for flux_kvsdir_get().
 *
 * Use flux_kvs_commit_anon() or flux_kvs_fence_anon() to commit the
 * default transaction.  Generally it is more clear to use an explicit
 * transaction.  Keys can be constructed by manually combining
 * kvsdir_key_at (dir) + "." + key.
 */

int flux_kvsdir_put (const flux_kvsdir_t *dir, const char *key,
                    const char *json_str)
                    __attribute__ ((deprecated));
int flux_kvsdir_pack (const flux_kvsdir_t *dir, const char *key,
                    const char *fmt, ...)
                    __attribute__ ((deprecated));
int flux_kvsdir_unlink (const flux_kvsdir_t *dir, const char *key)
                    __attribute__ ((deprecated));
int flux_kvsdir_mkdir (const flux_kvsdir_t *dir, const char *key)
                    __attribute__ ((deprecated));

/* Block until 'key' changes from value represented by '*json_str'.
 * 'json_str' is an IN/OUT parameter;  that is, it used to construct
 * the watch RPC, then upon receipt of a watch response, it is freed
 * and set to the new value.  Upon return, the caller should free
 * the new value.
 *
 * 'json_str' may initially point to a NULL value.  The function will
 * wait until 'key' exists then return its new value.
 *
 * If 'key' initially exists, then is removed, the function fails with
 * ENOENT and the initial value is not freed.
 */
int flux_kvs_watch_once (flux_t *h, const char *key, char **json_str)
                         __attribute__ ((deprecated));

/* Same as above except value is a directory pointed to by 'dirp'.
 */
int flux_kvs_watch_once_dir (flux_t *h, flux_kvsdir_t **dirp,
                             const char *fmt, ...)
                             __attribute__ ((format (printf, 3, 4), deprecated));

/* User callbacks for flux_kvs_watch() and flux_kvs_watch_dir() respectively.
 * The value passed to these functions is only valid for the duration of the
 * call.  'arg' and 'key' are the same as the arguments passed to the watch
 * functions.  If 'errnum' is non-zero, then the value is invalid; for example,
 * ENOENT - key no longer exists
 * ENOTDIR - key is not a directory (kvs_set_dir_f)
 * EISDIR - key is a directory (kvs_set_f)
 * These functions should normally return 0.  flux_reactor_stop_error() is
 * called internally if -1 is returned (errno must be set).
 */

typedef int (*kvs_set_f)(const char *key, const char *json_str, void *arg,
                         int errnum);
typedef int (*kvs_set_dir_f)(const char *key, flux_kvsdir_t *dir,
                             void *arg, int errnum);

/* Register 'set' callback on non-directory 'key'.
 * Callback is triggered once during registration to get the initial value.
 * Once the reactor is (re-)entered, it will then be called each time the
 * key changes.
 */
int flux_kvs_watch (flux_t *h, const char *key, kvs_set_f set, void *arg)
                    __attribute__ ((deprecated));

/* Register 'set' callback on directory 'key'.
 * Callback is triggered once during registration to get the initial value,
 * and thereafter, each time the directory changes. Note that due to the
 * KVS's hash tree namespace organization, this function will be called
 * whenever any key under this directory changes, since that forces the
 * hash references to change on parents, all the way to the root.
 */
int flux_kvs_watch_dir (flux_t *h, kvs_set_dir_f set, void *arg,
                        const char *fmt, ...)
                        __attribute__ ((format (printf, 4, 5), deprecated));

/* Cancel a flux_kvs_watch(), freeing server-side state, and unregistering
 * any callback.  Returns 0 on success, or -1 with errno set on error.
 */
int flux_kvs_unwatch (flux_t *h, const char *key)
                      __attribute__ ((deprecated));

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_KVS_CLASSIC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
