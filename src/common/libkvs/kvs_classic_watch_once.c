/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "kvs_classic.h"

#define CLASSIC_WATCH_FLAGS \
    (FLUX_KVS_WATCH \
   | FLUX_KVS_WAITCREATE \
   | FLUX_KVS_WATCH_FULL \
   | FLUX_KVS_WATCH_UNIQ)

#define CLASSIC_DIR_WATCH_FLAGS \
    (CLASSIC_WATCH_FLAGS \
   | FLUX_KVS_READDIR)

/* Synchronously cancel the stream of lookup responses.
 * Per RFC 6, once any error is returned, stream has ended.
 * This function destroys any value currently in future container.
 * If stream terminates with ENODATA, return 0, otherwise -1 with errno set.
 */
static int cancel_streaming_lookup (flux_future_t *f)
{
    flux_future_reset (f);
    if (flux_kvs_lookup_cancel (f) < 0)
        return -1; // N.B. future is unfulfilled - matchtag will not be released
    while (flux_kvs_lookup_get (f, NULL) == 0)
        flux_future_reset (f);
    if (errno != ENODATA)
        return -1;
    return 0;
}

/* Decode json values 'val1' and 'val2' and compare them, returning
 * true on a match.
 */
static bool match_json_value (const char *val1, const char *val2)
{
    json_t *o1 = json_loads (val1, JSON_DECODE_ANY, NULL);
    json_t *o2 = json_loads (val2, JSON_DECODE_ANY, NULL);
    bool match = false;

    if (o1 && o2 && json_equal (o1, o2))
        match = true;
    json_decref (o1);
    json_decref (o2);
    return match;
}

/* Compare two values, returning true on a match.
 * A value is assumed to be JSON for this earlier "classic" interface.
 * If the two values are NULL, or equal strings, it's a definitive match.
 * However non-equal strings have to be decoded and compared with json_equal(),
 * since equivalent JSON objects can be encoded with keys in different order.
 */
static bool match_value (const char *val1, const char *val2)
{
    if (!val1 && !val2)
        return true;
    if (val1 && val2) {
        if (!strcmp (val1, val2))
            return true;
        if (match_json_value (val1, val2))
            return true;
    }
    return false;
}

/* Synchronously consume lookup responses until one is received that does
 * NOT match 'oldval'.  On success, 'newval' is set to new value, and 0 is
 * returned.  On failure, return -1 with errno set.
 * N.B. stream must be cancelled on success, but not on failure.
 */
static int lookup_get_until_new (flux_future_t *f, const char *oldval,
                                                   const char **newval)
{
    while (flux_kvs_lookup_get (f, newval) == 0) {
        if (!match_value (oldval, *newval))
            return 0;
        flux_future_reset (f);
    }
    return -1;
}

/* same as above but for directories */
static int lookup_get_dir_until_new (flux_future_t *f,
                                     const flux_kvsdir_t *olddir,
                                     const flux_kvsdir_t **newdir)
{
    while (flux_kvs_lookup_get_dir (f, newdir) == 0) {
        if (!flux_kvsdir_equal (olddir, *newdir))
            return 0;
        flux_future_reset (f);
    }
    return -1;
}

int flux_kvs_watch_once (flux_t *h, const char *key, char **valp)
{
    flux_future_t *f;
    const char *newval;
    char *newval_cpy = NULL;

    if (!h || !key || !valp) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, CLASSIC_WATCH_FLAGS, key)))
        return -1;
    if (lookup_get_until_new (f, *valp, &newval) < 0)
        goto error;
    if (newval)
        newval_cpy = strdup (newval);
    if (cancel_streaming_lookup (f) < 0)
        goto error;
    if (newval && !newval_cpy) { // strdup failed
        errno = ENOMEM;
        goto error;
    }
    flux_future_destroy (f);
    free (*valp);
    *valp = newval_cpy;
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

static int watch_once_dir (flux_t *h, const char *key, flux_kvsdir_t **dirp)
{
    flux_future_t *f;
    const flux_kvsdir_t *newdir;
    flux_kvsdir_t *newdir_cpy;

    if (!(f = flux_kvs_lookup (h, CLASSIC_DIR_WATCH_FLAGS, key)))
        return -1;
    if (lookup_get_dir_until_new (f, *dirp, &newdir) < 0)
        goto error;
    newdir_cpy = flux_kvsdir_copy (newdir);
    if (cancel_streaming_lookup (f) < 0)
        goto error;
    if (!newdir_cpy) { // flux_kvsdir_copy failed
        errno = ENOMEM;
        goto error;
    }
    flux_future_destroy (f);
    flux_kvsdir_destroy (*dirp);
    *dirp = newdir_cpy;
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

int flux_kvs_watch_once_dir (flux_t *h, flux_kvsdir_t **dirp,
                             const char *fmt, ...)
{
    va_list ap;
    char *key;
    int rc;

    if (!h || !dirp || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_start (ap, fmt);
    rc = vasprintf (&key, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = ENOMEM;
        return -1;
    }
    rc = watch_once_dir (h, key, dirp);
    free (key);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
