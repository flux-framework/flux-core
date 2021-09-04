/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* codec.c - encode/decode functions betwen pmix data structures and json */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <jansson.h>
#include <pmix_server.h>

#include "src/common/libutil/log.h"
#include "src/common/libccan/ccan/base64/base64.h"

#include "codec.h"

json_t *pp_pointer_encode (void *ptr)
{
    return json_integer ((uintptr_t)ptr);
}

int pp_pointer_decode (json_t *o, void **ptr)
{
    if (!json_is_integer (o))
        return -1;
    *ptr = (void *)json_integer_value (o);
    return 0;
}

json_t *pp_data_encode (const void *data, size_t length)
{
    int xlength = base64_encoded_length (length) + 1; // +1 for \0 term
    char *xdata;
    json_t *o = NULL;

    if (!(xdata = malloc (xlength))
        || base64_encode (xdata, xlength, data, length) < 0
        || !(o = json_string (xdata))) {
        free (xdata);
        return NULL;
    }
    free (xdata);
    return o;
}

ssize_t pp_data_decode_bufsize (json_t *o)
{
    if (!json_is_string (o))
        return -1;

    int xlength = json_string_length (o);
    size_t length = base64_decoded_length (xlength);

    return length;
}

ssize_t pp_data_decode_tobuf (json_t *o, void *data, size_t length)
{
    if (!json_is_string (o))
        return -1;

    int xlength = json_string_length (o);
    const void *xdata = json_string_value (o);

    return base64_decode (data, length, xdata, xlength);
}

int pp_data_decode (json_t *o, void **datap, size_t *lengthp)
{
    ssize_t bufsize;
    ssize_t length;
    void *data;

    if ((bufsize = pp_data_decode_bufsize (o)) < 0)
        return -1;
    if (!(data = malloc (bufsize))
        || (length = pp_data_decode_tobuf (o, data, bufsize)) < 0) {
        free (data);
        return -1;
    }
    *datap = data;
    *lengthp = length;
    return 0;
}

json_t *pp_proc_encode (const pmix_proc_t *proc)
{
    return json_pack ("{s:s s:i}",
                      "nspace", proc->nspace,
                      "rank", proc->rank);
}

int pp_proc_decode (json_t *o, pmix_proc_t *proc)
{
    const char *nspace;
    int rank;

    if (json_unpack (o,
                     "{s:s s:i}",
                     "nspace", &nspace,
                     "rank", &rank) < 0)
        return -1;
    proc->rank = rank;
    strncpy (proc->nspace, nspace, PMIX_MAX_NSLEN);
    proc->nspace[PMIX_MAX_NSLEN] = '\0';
    return 0;
}

json_t *pp_proc_array_encode (const pmix_proc_t *procs, size_t nprocs)
{
    json_t *o;
    json_t *entry;
    int i;

    if (!(o = json_array ()))
        return NULL;
    for (i = 0; i < nprocs; i++) {
        if (!(entry = pp_proc_encode (&procs[i]))
            || json_array_append_new (o, entry) < 0) {
            json_decref (entry);
            json_decref (o);
            return NULL;
        }
    }
    return o;
}

int pp_proc_array_decode (json_t *o, pmix_proc_t **procsp, size_t *nprocsp)
{
    pmix_proc_t *procs;
    size_t nprocs = json_array_size (o);
    size_t index;
    json_t *value;

    if (!json_is_array (o)
        || !(procs = calloc (nprocs, sizeof (procs[0]))))
        return -1;
    json_array_foreach (o, index, value) {
        if (pp_proc_decode (value, &procs[index]) < 0) {
            free (procs);
            return -1;
        }
    }
    *procsp = procs;
    *nprocsp = nprocs;
    return 0;
}

json_t *pp_value_encode (const pmix_value_t *value)
{
    json_t *o = NULL;
    json_t *data = NULL;

    switch (value->type) {
        case PMIX_BOOL:
            data = value->data.flag ? json_true () : json_false ();
            break;
        case PMIX_BYTE:
            data = json_integer (value->data.byte);
            break;
        case PMIX_STRING:
            data = json_string (value->data.string);
            break;
        case PMIX_SIZE:
            data = json_integer (value->data.size);
            break;
        case PMIX_PID:
            data = json_integer (value->data.pid);
            break;
        case PMIX_INT:
            data = json_integer (value->data.integer);
            break;
        case PMIX_INT8:
            data = json_integer (value->data.int8);
            break;
        case PMIX_INT16:
            data = json_integer (value->data.int16);
            break;
        case PMIX_INT32:
            data = json_integer (value->data.int32);
            break;
        case PMIX_INT64:
            data = json_integer (value->data.int64);
            break;
        case PMIX_UINT:
            data = json_integer (value->data.uint);
            break;
        case PMIX_UINT8:
            data = json_integer (value->data.uint8);
            break;
        case PMIX_UINT16:
            data = json_integer (value->data.uint16);
            break;
        case PMIX_UINT32:
            data = json_integer (value->data.uint32);
            break;
        case PMIX_UINT64:
            data = json_integer (value->data.uint64);
            break;
        case PMIX_FLOAT:
            data = json_real (value->data.fval);
            break;
        case PMIX_DOUBLE:
            data = json_real (value->data.dval);
            break;
        case PMIX_TIMEVAL: // struct timeval
            data = json_pack ("{s:I s:I}",
                              "sec", (intmax_t)value->data.tv.tv_sec,
                              "usec", (intmax_t)value->data.tv.tv_usec);
            break;
        case PMIX_TIME: // time_t
            data = json_integer (value->data.time);
            break;
        case PMIX_STATUS: // pmix_status_t
            data = json_integer (value->data.status);
            break;
        case PMIX_PROC: // pmix_proc_t *
            data = pp_proc_encode (value->data.proc);
            break;
        default:
            log_msg ("pmix: unsupported value encoding %d", value->type);
            break;
    }
    if (data) {
        o = json_pack ("{s:i s:O}",
                       "type", value->type,
                       "data", data);
        json_decref (data);
    }
    return o;
}

void pp_value_release (pmix_value_t *value)
{
    switch (value->type) {
        case PMIX_PROC:
            free (value->data.proc);
            break;
        case PMIX_STRING:
            free (value->data.string);
            break;
    }
}

/* N.B. for some types, memory is allocated and assigned to value->data
 * that must be freed with pp_value_release().
 */
int pp_value_decode (json_t *o, pmix_value_t *value)
{
    int type;
    json_t *data;

    if (json_unpack (o,
                     "{s:i s:o}",
                     "type", &type,
                     "data", &data) < 0)
        return -1;
    switch (type) {
        case PMIX_BOOL:
            value->data.flag = json_is_false (data) ? 0 : 1;
            break;
        case PMIX_BYTE:
            value->data.byte = json_integer_value (data);
            break;
        case PMIX_STRING: {
            char *cpy;
            if (!(cpy = strdup (json_string_value (data))))
                return -1;
            value->data.string = cpy;
            break;
        }
        case PMIX_SIZE:
            value->data.size = json_integer_value (data);
            break;
        case PMIX_PID:
            value->data.pid = json_integer_value (data);
            break;
        case PMIX_INT:
            value->data.integer = json_integer_value (data);
            break;
        case PMIX_INT8:
            value->data.int8 = json_integer_value (data);
            break;
        case PMIX_INT16:
            value->data.int16 = json_integer_value (data);
            break;
        case PMIX_INT32:
            value->data.int32 = json_integer_value (data);
            break;
        case PMIX_INT64:
            value->data.int64 = json_integer_value (data);
            break;
        case PMIX_UINT:
            value->data.uint = json_integer_value (data);
            break;
        case PMIX_UINT8:
            value->data.uint8 = json_integer_value (data);
            break;
        case PMIX_UINT16:
            value->data.uint16 = json_integer_value (data);
            break;
        case PMIX_UINT32:
            value->data.uint32 = json_integer_value (data);
            break;
        case PMIX_UINT64:
            value->data.uint64 = json_integer_value (data);
            break;
        case PMIX_FLOAT:
            value->data.fval = json_real_value (data);
            break;
        case PMIX_DOUBLE:
            value->data.dval = json_real_value (data);
            break;
        case PMIX_TIMEVAL: {
            json_int_t sec, usec;
            if (json_unpack (data, "{s:I s:I}", &sec, &usec) < 0)
                return -1;
            value->data.tv.tv_sec = sec;
            value->data.tv.tv_usec = usec;
            break;
        }
        case PMIX_TIME: // time_t
            value->data.time = json_integer_value (data);
            break;
        case PMIX_STATUS: // pmix_status_t
            value->data.status = json_integer_value (data);
            break;
        case PMIX_PROC: {
            pmix_proc_t *proc;
            if (!(proc = calloc (1, sizeof (*proc)))
                || pp_proc_decode (data, proc) < 0) {
                free (proc);
                return -1;
            }
            value->data.proc = proc;
            break;
        }
        default:
            return -1;
    }
    value->type = type;
    return 0;
}

json_t *pp_info_encode (const pmix_info_t *info)
{
    json_t *o;
    json_t *value;

    if (!(value = pp_value_encode (&info->value)))
        return NULL;
    o = json_pack ("{s:s s:i s:O}",
                   "key", info->key,
                   "flags", info->flags,
                   "value", value);
    json_decref (value);
    return o;
}

void pp_info_release (pmix_info_t *info)
{
    pp_value_release (&info->value);
}

int pp_info_decode (json_t *o, pmix_info_t *info)
{
    const char *key;
    int flags;
    json_t *xvalue;

    if (json_unpack (o,
                     "{s:s s:i s:o}",
                     "key", &key,
                     "flags", &flags,
                     "value", &xvalue) < 0)
        return -1;
    if (pp_value_decode (xvalue, &info->value) < 0) // allocs mem
        return -1;
    strncpy (info->key, key, PMIX_MAX_KEYLEN);
    info->key[PMIX_MAX_KEYLEN] = '\0';
    return 0;
}

json_t *pp_info_array_encode (const pmix_info_t *info, size_t ninfo)
{
    json_t *o;
    json_t *entry;
    int i;

    if (!(o = json_array ()))
        return NULL;
    for (i = 0; i < ninfo; i++) {
        if (!(entry = pp_info_encode (&info[i]))
            || json_array_append_new (o, entry) < 0) {
            json_decref (entry);
            json_decref (o);
            return NULL;
        }
    }
    return o;
}

// vi:tabstop=4 shiftwidth=4 expandtab
