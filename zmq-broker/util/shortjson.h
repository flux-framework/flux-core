#ifndef _HAVE_UTIL_SHORTJSON_H
#define _HAVE_UTIL_SHORTJSON_H

typedef json_object *J;

static __inline__ J
Jnew (void)
{
    return util_json_object_new_object ();
}

static __inline__ J
Jget (J o)
{
    return o ? json_object_get (o) : o;
}

static __inline__ void
Jput (J o)
{
    json_object_put (o);
}

static __inline__ void
Jadd_int (J o, const char *name, int i)
{
    util_json_object_add_int (o, (char *)name, i);
}

static __inline__ void
Jadd_str (J o, const char *name, const char *s)
{
    util_json_object_add_string (o, (char *)name, s);
}

static __inline__ void
Jadd_obj (J o, const char *name, J obj)
{
    json_object_object_add (o, (char *)name, Jget (obj));
}

#endif
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
