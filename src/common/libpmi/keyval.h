/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_PMI_KEYVAL_H
#    define _FLUX_PMI_KEYVAL_H

enum {
    EKV_SUCCESS = 0,
    EKV_NOKEY = -1,       /* key cannot be found in input string */
    EKV_VAL_LEN = -2,     /* value longer than provided 'len' argument */
    EKV_VAL_NOMATCH = -3, /* value does not match 'match' argument */
    EKV_VAL_PARSE = -4,   /* error parsing value */
};

/* Parse a null-terminated input string, interpreted a sequence of
 * key=value tuples, separated by whitespace.  The string may or
 * may not end in a newline (it is ignored).
 *
 * key=int
 *    value is an integer, with optional leading sign
 * key=uint
 *    value is an unsigned integer
 * key=word
 *    value is a string that may contain = but not whitespace
 * key=string
 *    value is a string that may contain = or whitespaces; it must be the
 *    last tuple in the input string.
 *
 * Functions return EKV_SUCCESS on success, one of the negative EKV_
 * values on falure.
 */

int keyval_parse_int (const char *s, const char *key, int *val);
int keyval_parse_uint (const char *s, const char *key, unsigned int *val);
int keyval_parse_word (const char *s, const char *key, char *val, int len);
int keyval_parse_isword (const char *s, const char *key, const char *match);
int keyval_parse_string (const char *s, const char *key, char *val, int len);

#endif /* !_FLUX_PMI_KEYVAL_H */

/*
 * vi: ts=4 sw=4 expandtab
 */
