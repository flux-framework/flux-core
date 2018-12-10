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

/* An idset is an internally sorted set of non-negative integers  (0,1,2,3...).
 */


#ifndef FLUX_IDSET_H
#define FLUX_IDSET_H

#include <sys/types.h>

enum idset_flags {
    IDSET_FLAG_AUTOGROW = 1, // allow idset size to automatically grow
    IDSET_FLAG_BRACKETS = 2, // encode non-singleton idset with brackets
    IDSET_FLAG_RANGE = 4,    // encode with ranges ("2,3,4,8" -> "2-4,8")
};

/* Create/destroy an idset.
 * Set the initial size to 'size' (0 means implementation uses a default size).
 * If 'flags' includes IDSET_FLAG_AUTOGROW, the idset is resized to fit if
 * an id >= size is set.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_create (size_t size, int flags);
void idset_destroy (struct idset *idset);

/* Encode 'idset' to a string, which the caller must free.
 * 'flags' may include IDSET_FLAG_BRACKETS, IDSET_FLAG_RANGE.
 * Returns string on success, or NULL on failure with errno set.
 */
char *idset_encode (const struct idset *idset, int flags);

/* Decode string 's' to an idset.
 * Returns idset on success, or NULL on failure with errno set.
 */
struct idset *idset_decode (const char *s);

/* Make id a member of idset.
 * Return 0 on success, -1 on failure with errno set.
 */
int idset_set (struct idset *idset, unsigned int id);

#endif /* !FLUX_IDSET_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
