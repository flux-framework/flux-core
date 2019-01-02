/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "pmi.h"
#include "pmi_strerror.h"


typedef struct {
    int errnum;
    const char *errstr;
} etab_t;

static etab_t pmi_errors[] = {
    { PMI_SUCCESS,              "operation completed successfully" },
    { PMI_FAIL,                 "operation failed" },
    { PMI_ERR_NOMEM,            "input buffer not large enough" },
    { PMI_ERR_INIT,             "PMI not initialized" },
    { PMI_ERR_INVALID_ARG,      "invalid argument" },
    { PMI_ERR_INVALID_KEY,      "invalid key argument" },
    { PMI_ERR_INVALID_KEY_LENGTH,"invalid key length argument" },
    { PMI_ERR_INVALID_VAL,      "invalid val argument" },
    { PMI_ERR_INVALID_VAL_LENGTH,"invalid val length argument" },
    { PMI_ERR_INVALID_LENGTH,   "invalid length argument" },
    { PMI_ERR_INVALID_NUM_ARGS, "invalid number of arguments" },
    { PMI_ERR_INVALID_ARGS,     "invalid args argument" },
    { PMI_ERR_INVALID_NUM_PARSED, "invalid num_parsed length argument" },
    { PMI_ERR_INVALID_KEYVALP,  "invalid keyvalp argument" },
    { PMI_ERR_INVALID_SIZE,     "invalid size argument" },
};
static const int pmi_errors_len = sizeof (pmi_errors) / sizeof (pmi_errors[0]);

const char *pmi_strerror (int rc)
{
    static char unknown[] = "pmi error XXXXXXXXX";
    int i;

    for (i = 0; i < pmi_errors_len; i++) {
        if (pmi_errors[i].errnum == rc)
            return pmi_errors[i].errstr;
    }
    snprintf (unknown, sizeof (unknown), "pmi error %d", rc);
    return unknown;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
