#ifndef _JSTATCTL_SHORTJANSSON_H
#define _JSTATCTL_SHORTJANSSON_H

#include <jansson.h>
#include <stdbool.h>
#include "src/common/libutil/oom.h"

/* Creates JSON object with refcount of 1.
 */
static __inline__ json_t *
Jnew (void)
{
    json_t *n = json_object ();
    if (!n)
        oom ();
    return n;
}

/* Increment JSON object refcount.
 */
static __inline__ json_t *
Jget (json_t *o)
{
    return json_incref (o);
}

/* Decrement JSON object refcount and free if refcount == 0.
 */
static __inline__ void
Jput (json_t *o)
{
    json_decref (o);
}

/* Add bool to JSON.
 */
static __inline__ void
Jadd_bool (json_t *o, const char *name, bool b)
{
    json_t *n = b ? json_true () : json_false ();
    if (!n)
        oom ();
    json_object_set_new (o, name, n);
}

/* Add integer to JSON.
 */
static __inline__ void
Jadd_int (json_t *o, const char *name, int i)
{
    json_t *n = json_integer (i);
    if (!n)
        oom ();
    json_object_set_new (o, name, n);
}

/* Add 64bit integer to JSON.
 */
static __inline__ void
Jadd_int64 (json_t *o, const char *name, int64_t i)
{
    json_t *n = json_integer (i);
    if (!n)
        oom ();
    json_object_set_new (o, name, n);
}

/* Add double to JSON.
 */
static __inline__ void
Jadd_double (json_t *o, const char *name, double d)
{
    json_t *n = json_real (d);
    if (!n)
        oom ();
    json_object_set_new (o, name, n);
}

/* Add string to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_str (json_t *o, const char *name, const char *s)
{
    json_t *n = json_string (s);
    if (!n)
        oom ();
    json_object_set_new (o, name, n);
}

/* Add object to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_obj (json_t *o, const char *name, json_t *obj)
{
    json_object_set (o, name, obj);
}

/* Wrapper for json_t_object_get_ex() (borrowed reference)
 */
static __inline__ json_t *
Jobj_get (json_t *o, const char *name)
{
    return json_object_get (o, name);
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int (json_t *o, const char *name, int *ip)
{
    if (json_unpack (o, "{s:i}", name, ip) < 0)
        return false;
    return true;
}

/* Get double from JSON.
 */
static __inline__ bool
Jget_double (json_t *o, const char *name, double *dp)
{
    if (json_unpack (o, "{s:f}", name, dp) < 0)
        return false;
    return true;
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int64 (json_t *o, const char *name, int64_t *ip)
{
    json_int_t ji;
    if (json_unpack (o, "{s:I}", name, &ji) < 0)
        return false;
    *ip = (int64_t) ji;
    return true;
}

/* Get string from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_str (json_t *o, const char *name, const char **sp)
{
    if (json_unpack (o, "{s:s}", name, sp) < 0)
        return false;
    return true;
}

/* Get object from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_obj (json_t *o, const char *name, json_t **op)
{
    if (json_unpack (o, "{s:o}", name, op) < 0)
        return false;
    return true;
}

/* Get boolean from JSON.
 */
static __inline__ bool
Jget_bool (json_t *o, const char *name, bool *bp)
{
    if (json_unpack (o, "{s:b}", name, bp) < 0)
        return false;
    return true;
}

/* Create new JSON array.
 */
static __inline__ json_t *
Jnew_ar (void)
{
    json_t *a = json_array ();
    if (!a)
        oom ();
    return a;
}

/* Add object to JSON array (caller retains ownership of original).
 */
static __inline__ void
Jadd_ar_obj (json_t *o, json_t *obj)
{
    //assert (json_t_get_type (o) == json_type_array)
    json_array_append (o, obj);
}

/* Get JSON array length.
 */
static __inline__ bool
Jget_ar_len (json_t *o, int *ip)
{
    if (!json_is_array (o))
        return false;
    if (ip)
        *ip = json_array_size (o);
    return true;
}

/* Get JSON object at index 'n' array.
 */
static __inline__ bool
Jget_ar_obj (json_t *o, int n, json_t **op)
{
    if (!json_is_array (o))
        return false;
    if (n < 0 || n > (int)json_array_size (o))
        return false;
    if (op)
        *op = json_array_get (o, n);
    return true;
}

/* Get integer at index 'n' of array.
 */
static __inline__ bool
Jget_ar_int (json_t *o, int n, int *ip)
{
    json_t *m;
    if (!Jget_ar_obj (o, n, &m))
        return false;
    if (ip)
        *ip = json_integer_value (m);
    return true;
}

/* Get string at index 'n' of array.
 */
static __inline__ bool
Jget_ar_str (json_t *o, int n, const char **sp)
{
    json_t *m;
    if (!Jget_ar_obj (o, n, &m))
        return false;
    if (sp)
        *sp = json_string_value (m);
    return true;
}

/* Decode string to JSON (caller is given ownership).
 */
static __inline__ json_t *
Jfromstr (const char *s)
{
    json_error_t err;
    json_t *o = json_loads (s, JSON_DECODE_ANY, &err);
    /* XXX: what to do with error object? */
    return (o);
}

#endif /* _LIBJSC_SHORTJANSON_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
