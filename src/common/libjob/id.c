/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <flux/core.h>

#include "job.h"

#include "ccan/str/str.h"
#include "src/common/libutil/fluid.h"

int flux_job_id_parse (const char *s, flux_jobid_t *idp)
{
    int len;
    const char *p = s;
    if (s == NULL
        || idp == NULL
        || (len = strlen (s)) == 0) {
        errno = EINVAL;
        return -1;
    }
    /*  Remove leading whitespace
     */
    while (isspace(*p))
        p++;
    /*  Ignore any `job.` prefix. This allows a "kvs" encoding
     *   created by flux_job_id_encode(3) to properly decode.
     */
    if (strstarts (p, "job."))
        p += 4;
    return fluid_parse (p, idp);
}

int flux_job_id_encode (flux_jobid_t id,
                        const char *type,
                        char *buf,
                        size_t bufsz)
{
    fluid_string_type_t t;
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (type == NULL || strcasecmp (type, "dec") == 0) {
        int len = snprintf (buf, bufsz, "%ju", (uintmax_t) id);
        if (len >= bufsz) {
            errno = EOVERFLOW;
            return -1;
        }
        return 0;
    }
    if (strcasecmp (type, "hex") == 0) {
        int len = snprintf (buf, bufsz, "0x%jx", (uintmax_t) id);
        if (len >= bufsz) {
            errno = EOVERFLOW;
            return -1;
        }
        return 0;
    }

    /* The following encodings all use fluid_encode(3).
     */
    if (strcasecmp (type, "kvs") == 0) {
        /* kvs: prepend "job." to "dothex" encoding.
         */
        int len = snprintf (buf, bufsz, "job.");
        if (len >= bufsz) {
            errno = EOVERFLOW;
            return -1;
        }
        buf += len;
        bufsz -= len;
        type = "dothex";
    }
    if (strcasecmp (type, "dothex") == 0)
        t = FLUID_STRING_DOTHEX;
    else if (strcasecmp (type, "words") == 0)
        t = FLUID_STRING_MNEMONIC;
    else if (strcasecmp (type, "f58") == 0)
        t = FLUID_STRING_F58;
    else if (strcasecmp (type, "emoji") == 0)
        t = FLUID_STRING_EMOJI;
    else {
        /*  Return EPROTO for invalid type to differentiate from
         *   other invalid arguments.
         */
        errno = EPROTO;
        return -1;
    }
    return fluid_encode (buf, bufsz, id, t);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
