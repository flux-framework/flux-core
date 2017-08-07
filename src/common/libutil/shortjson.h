#ifndef _UTIL_SHORTJSON_H
#define _UTIL_SHORTJSON_H

#include <stdbool.h>
#include "src/common/libjson-c/json.h"
#include "oom.h"

/* Creates JSON object with refcount of 1.
 */
static __inline__ json_object *
Jnew (void)
{
    json_object *n = json_object_new_object ();
    if (!n)
        oom ();
    return n;
}

/* Increment JSON object refcount.
 */
static __inline__ json_object *
Jget (json_object *o)
{
    return o ? json_object_get (o) : o;
}

/* Decrement JSON object refcount and free if refcount == 0.
 */
static __inline__ void
Jput (json_object *o)
{
    if (o)
        json_object_put (o);
}

/* Add integer to JSON.
 */
static __inline__ void
Jadd_int (json_object *o, const char *name, int i)
{
    json_object *n = json_object_new_int (i);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add 64bit integer to JSON.
 */
static __inline__ void
Jadd_int64 (json_object *o, const char *name, int64_t i)
{
    json_object *n = json_object_new_int64 (i);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add double to JSON.
 */
static __inline__ void
Jadd_double (json_object *o, const char *name, double d)
{
    json_object *n = json_object_new_double (d);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add string to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_str (json_object *o, const char *name, const char *s)
{
    json_object *n = json_object_new_string (s);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

static __inline__ void
Jadd_str_len (json_object *o, const char *name, const char *s, int len)
{
    json_object *n = json_object_new_string_len (s, len);
    if (!n)
        oom ();
    json_object_object_add (o, (char *)name, n);
}

/* Add object to JSON (caller retains ownership of original).
 */
static __inline__ void
Jadd_obj (json_object *o, const char *name, json_object *obj)
{
    json_object_object_add (o, (char *)name, Jget (obj));
}

/* Wrapper for json_object_object_get_ex()
 */
static __inline__ json_object *
Jobj_get (json_object *o, const char *name)
{
    json_object *n = NULL;
    json_object_object_get_ex (o, (char *)name, &n);
    return (n);
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int (json_object *o, const char *name, int *ip)
{
    json_object *n = Jobj_get (o, name);
    if (n && ip)
        *ip = json_object_get_int (n);
    return (n != NULL);
}

/* Get double from JSON.
 */
static __inline__ bool
Jget_double (json_object *o, const char *name, double *dp)
{
    json_object *n = Jobj_get (o, name);
    if (n && dp)
        *dp = json_object_get_double (n);
    return (n != NULL);
}

/* Get integer from JSON.
 */
static __inline__ bool
Jget_int64 (json_object *o, const char *name, int64_t *ip)
{
    json_object *n = Jobj_get (o, name);
    if (n && ip)
        *ip = json_object_get_int64 (n);
    return (n != NULL);
}

/* Get string from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_str (json_object *o, const char *name, const char **sp)
{
    json_object *n = Jobj_get (o, name);
    if (n && sp)
        *sp = json_object_get_string (n);
    return (n != NULL);
}

/* Get object from JSON (still owned by JSON, do not free).
 */
static __inline__ bool
Jget_obj (json_object *o, const char *name, json_object **op)
{
    json_object *n = Jobj_get (o, name);
    if (n && op)
        *op = n;
    return (n != NULL);
}

/* Create new JSON array.
 */
static __inline__ json_object *
Jnew_ar (void)
{
    json_object *a = json_object_new_array ();
    if (!a)
        oom ();
    return a;
}

/* Add object to JSON array (caller retains ownership of original).
 */
static __inline__ void
Jadd_ar_obj (json_object *o, json_object *obj)
{
    //assert (json_object_get_type (o) == json_type_array)
    json_object_array_add (o, Jget (obj));
}

static __inline__ void
Jput_ar_obj (json_object *o, int n, json_object *obj)
{
    //assert (json_object_get_type (o) == json_type_array)
    json_object_array_put_idx (o, n, Jget (obj));
}

static __inline__ void
Jadd_ar_int (json_object *o, int i)
{
    json_object *p = json_object_new_int (i);
    if (!p)
        oom ();
    json_object_array_add (o, p);
}

static __inline__ void
Jadd_ar_str (json_object *o, const char *s)
{
    json_object *p = json_object_new_string (s);
    if (!p)
        oom ();
    json_object_array_add (o, p);
}

/* Get JSON array length.
 */
static __inline__ bool
Jget_ar_len (json_object *o, int *ip)
{
    if (json_object_get_type (o) != json_type_array)
        return false;
    if (ip)
        *ip = json_object_array_length (o);
    return true;
}

/* Get JSON object at index 'n' array.
 */
static __inline__ bool
Jget_ar_obj (json_object *o, int n, json_object **op)
{
    if (json_object_get_type (o) != json_type_array)
        return false;
    if (n < 0 || n > json_object_array_length (o))
        return false;
    if (op)
        *op = json_object_array_get_idx (o, n);
    return true;
}

/* Get integer at index 'n' of array.
 */
static __inline__ bool
Jget_ar_int (json_object *o, int n, int *ip)
{
    json_object *m;

    if (json_object_get_type (o) != json_type_array)
        return false;
    if (n < 0 || n > json_object_array_length (o))
        return false;
    if (!(m = json_object_array_get_idx (o, n)))
        return false;
    if (ip)
        *ip = json_object_get_int (m);
    return true;
}

/* Get string at index 'n' of array.
 */
static __inline__ bool
Jget_ar_str (json_object *o, int n, const char **sp)
{
    json_object *m;

    if (json_object_get_type (o) != json_type_array)
        return false;
    if (n < 0 || n > json_object_array_length (o))
        return false;
    if (!(m = json_object_array_get_idx (o, n)))
        return false;
    if (sp)
        *sp = json_object_get_string (m);
    return true;
}

/* Encode JSON to string (owned by JSON, do not free)
 */
static __inline__ const char *
Jtostr (json_object *o)
{
    return o ? json_object_to_json_string (o) : NULL;
}

/* Decode string to JSON (caller is given ownership).
 */
static __inline__ json_object *
Jfromstr (const char *s)
{
    return json_tokener_parse (s);
}

static __inline__ void
Jmerge (json_object *dst, json_object *src)
{
    json_object_iter iter;

    json_object_object_foreachC (src, iter) {
        json_object_object_del (dst, iter.key); /* necessary? */
        json_object_object_add (dst, iter.key, Jget (iter.val));
    }
}

static __inline__ json_object *
Jdup (json_object *o)
{
    return o ? Jfromstr (Jtostr (o)) : NULL;
}

#endif /* _UTIL_SHORTJSON_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
