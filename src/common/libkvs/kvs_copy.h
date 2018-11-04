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

#ifndef _FLUX_CORE_KVS_COPY_H
#define _FLUX_CORE_KVS_COPY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create a copy of 'srckey' at 'dstkey'.
 * Due to the hash-tree design of the KVS, dstkey is by definition a
 * "deep copy" (or writable snapshot) of all content below srckey.
 * The copy operation has a low overhead since it only copies a single
 * directory entry.  'srckey' and 'dstkey' may be in different namespaces.
 * Returns future on success, NULL on failure with errno set.
 */
flux_future_t *flux_kvs_copy (flux_t *h, const char *srckey,
                                         const char *dstkey,
                                         int commit_flags);

/* Move 'srckey' to 'dstkey'.
 * This is a copy followed by an unlink on 'srckey'.
 * 'srckey' and 'dstkey' may be in different namespaces.
 * The copy and unlink are not atomic.
 * Returns future on success, NULL on failure with errno set.
 */
flux_future_t *flux_kvs_move (flux_t *h, const char *srckey,
                                         const char *dstkey,
                                         int commit_flags);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_COPY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
