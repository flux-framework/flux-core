/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
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

#ifndef _FLUX_CORE_KVS_WATCH_H
#define _FLUX_CORE_KVS_WATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Watch a KVS key for changes.
 *
 * There are two distinct interfaces, one that registers a callback that
 * is triggered when the key changes, and one that accepts an initial value
 * and returns a new value when it changes.
 *
 * Use flux_kvs_lookup() with watch flags instead.  These interfaces are
 * deprecated.
 */

enum kvs_watch_flags {
    KVS_WATCH_ONCE = 4,
    KVS_WATCH_FIRST = 8,
};

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
typedef int (*kvs_set_dir_f)(const char *key, flux_kvsdir_t *dir, void *arg,
                             int errnum);

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
                        __attribute__ ((format (printf, 4, 5),deprecated));

/* Cancel a flux_kvs_watch(), freeing server-side state, and unregistering
 * any callback.  Returns 0 on success, or -1 with errno set on error.
 */
int flux_kvs_unwatch (flux_t *h, const char *key)
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
                             __attribute__ ((format (printf, 3, 4),deprecated));

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_WATCH_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
