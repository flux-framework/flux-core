/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "log.h"
#include "xzmalloc.h"
#include "shortjson.h"
#include "base64.h"
#include "base64_json.h"

json_object *base64_json_encode (uint8_t *dat, int len)
{
    char *buf;
    int dstlen;
    json_object *o;
    int size = base64_encode_length (len);

    buf = xzmalloc (size);
    (void) base64_encode_block (buf, &dstlen, dat, len);
    if (!(o = json_object_new_string (buf)))
        oom ();
    free (buf);
    return o;
}

int base64_json_decode (json_object *o, uint8_t **datp, int *lenp)
{
    const char *s;
    int dlen, len;
    void *dst;

    if (!o || json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        return -1;
    }
    s = json_object_get_string (o);
    len = strlen (s);
    dst = xzmalloc (base64_decode_length (len));
    if (base64_decode_block (dst, &dlen, s, len) < 0) {
        free (dst);
        dst = NULL;
        errno = EINVAL;
        return -1;
    }

    *lenp = dlen;
    *datp = (uint8_t *) dst;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
