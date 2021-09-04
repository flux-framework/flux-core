/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <jansson.h>
#include <pmix_server.h>

#ifndef _PMIX_PP_CODEC_H
#define _PMIX_PP_CODEC_H

json_t *pp_pointer_encode (void *ptr);
int pp_pointer_decode (json_t *o, void **ptr);

json_t *pp_data_encode (const void *data, size_t length);
int pp_data_decode (json_t *o, void **data, size_t *length);

ssize_t pp_data_decode_bufsize (json_t *o);
ssize_t pp_data_decode_tobuf (json_t *o, void *data, size_t buflen);

json_t *pp_value_encode (const pmix_value_t *value);
int pp_value_decode (json_t *o, pmix_value_t *value); // allocs internal mem
void pp_value_release (pmix_value_t *value); // release internal mem from decode

json_t *pp_info_encode (const pmix_info_t *info);
int pp_info_decode (json_t *o, pmix_info_t *info); // allocs internal mem
void pp_info_release (pmix_info_t *info); // release internal mem from decode

json_t *pp_proc_encode (const pmix_proc_t *proc);
int pp_proc_decode (json_t *o, pmix_proc_t *proc);

json_t *pp_proc_array_encode (const pmix_proc_t *procs, size_t nprocs);
int pp_proc_array_decode (json_t *o, pmix_proc_t **procs, size_t *nprocs);

// decode info array with infovec_create_from_json()
json_t *pp_info_array_encode (const pmix_info_t *info, size_t ninfo);

#endif

// vi:tabstop=4 shiftwidth=4 expandtab
