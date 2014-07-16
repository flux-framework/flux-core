#ifndef _UTIL_SHORTJSON_H
#define _UTIL_SHORTJSON_H

#include <json.h>
#include <stdbool.h>
#include "log.h" /* oom */

typedef json_object *JSON;

/* Creates JSON object with refcount of 1.
 */
static __inline__ JSON
Jnew (void)
{
    JSON n = json_object_new_object ();
    if (!n)
        oom ();
    return n;
}

/* Increment JSON object refcount.
 */
static __inline__ JSON
Jget (JSON o)
{
    return o ? json_object_get (o) : o;
}

/* Decrement JSON object refcount and free if refcount == 0.
 */
static __inline__ void
Jput (JSON o)
{
    if (o)
        json_object_put (o);
}

/* Add bool to JSON.
 */
static __inline__ void
Jadd_bool (JSON o, const char *name, bool b)
{
    JSON n = json_object_new_boolean (b);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add integer to JSON.
 */
static __inline__ void
Jadd_int (JSON o, const char *name, int i)
{
    JSON n = json_object_new_int (i);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add double to JSON.
 */
static __inline__ void
Jadd_double (JSON o, const char *name, double d)
{
    JSON n = json_object_new_double (d);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add string to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_str (JSON o, const char *name, const char *s)
{
    JSON n = json_object_new_string (s);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add object to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_obj (JSON o, const char *name, JSON obj)
{
    json_object_object_add (o, (char *)name, Jget (obj));
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int (JSON o, const char *name, int *ip)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *ip = json_object_get_int (n);
    return (n != NULL);
}

/* Get double from JSON.
 */
static __inline__ bool
Jget_double (JSON o, const char *name, double *dp)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *dp = json_object_get_double (n);
    return (n != NULL);
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int64 (JSON o, const char *name, int64_t *ip)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *ip = json_object_get_int64 (n);
    return (n != NULL);
}

/* Get string from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_str (JSON o, const char *name, const char **sp)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *sp = json_object_get_string (n);
    return (n != NULL);
}

/* Get object from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_obj (JSON o, const char *name, JSON *op)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *op = n;
    return (n != NULL);
}

/* Get boolean from JSON.
 */
static __inline__ bool
Jget_bool (JSON o, const char *name, bool *bp)
{
    JSON n = json_object_object_get (o, (char *)name);
    if (n)
        *bp = json_object_get_boolean (n);
    return (n != NULL);
}

/* Create new JSON array.
 */
static __inline__ JSON
Jnew_ar (void)
{
    JSON a = json_object_new_array ();
    if (!a)
        oom ();
    return a;
}

/* Add object to JSON array (caller retains ownership of original).
 */
static __inline__ void
Jadd_ar_obj (JSON o, JSON obj)
{
    //assert (json_object_get_type (o) == json_type_array)
    json_object_array_add (o, Jget (obj));
}

static __inline__ void
Jput_ar_obj (JSON o, int n, JSON obj)
{
    //assert (json_object_get_type (o) == json_type_array)
    json_object_array_put_idx (o, n, Jget (obj));
}

static __inline__ void
Jadd_ar_int (JSON o, int i)
{
    JSON p = json_object_new_int (i);
    if (!p)
        oom ();
    json_object_array_add (o, p);
}

/* Get JSON array length.
 */
static __inline__ bool
Jget_ar_len (JSON o, int *ip)
{
    if (json_object_get_type (o) != json_type_array)
        return false;
    *ip = json_object_array_length (o);
    return true;
}

/* Get JSON object at index 'n' array.
 */
static __inline__ bool
Jget_ar_obj (JSON o, int n, JSON *op)
{
    if (json_object_get_type (o) != json_type_array)
        return false;
    if (n < 0 || n > json_object_array_length (o))
        return false;
    *op = json_object_array_get_idx (o, n);
    return true;
}

/* Get integer at index 'n' of array.
 */
static __inline__ bool
Jget_ar_int (JSON o, int n, int *ip)
{
    JSON m;

    if (json_object_get_type (o) != json_type_array)
        return false;
    if (n < 0 || n > json_object_array_length (o))
        return false;
    if (!(m = json_object_array_get_idx (o, n)))
        return false;
    *ip = json_object_get_int (m);
    return true;
}

/* Encode JSON to string (owned by JSON, do not free)
 */
static __inline__ const char *
Jtostr (JSON o)
{
    return o ? json_object_to_json_string (o) : NULL;
}

/* Decode string to JSON (caller is given ownership).
 */
static __inline__ JSON
Jfromstr (const char *s)
{
    return json_tokener_parse (s);
}

static __inline__ void
Jmerge (JSON dst, JSON src)
{
    json_object_iter iter;

    json_object_object_foreachC (src, iter) {
        json_object_object_del (dst, iter.key); /* necessary? */
        json_object_object_add (dst, iter.key, Jget (iter.val));
    }
}

static __inline__ JSON
Jdup (JSON o)
{
    return o ? Jfromstr (Jtostr (o)) : NULL;
}

#endif /* _UTIL_SHORTJSON_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
