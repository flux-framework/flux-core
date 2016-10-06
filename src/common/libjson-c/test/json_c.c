#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjson-c/json.h"
#include "src/common/libjson-c/json_object_private.h" /* for refcounts */

const char *json1 = "{"
    "\"a\":42,"
    "\"pi\":3.14159,"
    "\"name\":\"jangles\","
    "\"record\":{"
        "\"subfoo\":true"
    "}"
"}";

void string_test (const char *value, const char *desc)
{
    json_object *o, *x;
    const char *s;

    o = json_object_new_string (value);
    ok (o != NULL && json_object_get_type (o) == json_type_string,
        "%s: json_object_new_string works", desc);
    s = json_object_to_json_string (o);
    diag ("%s: %s", desc, s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "%s: json_tokener_parse worked", desc);
    ok (json_object_get_type (x) == json_type_string,
        "%s: parsed object is still type string", desc);
    s = json_object_get_string (x);
    ok ((value == NULL && s == NULL)
        || (s != NULL && value != NULL && !strcmp (s, value)),
        "%s: parsed object is correct value", desc);
    json_object_put (o);
    json_object_put (x);
}

void null_test (void)
{
    json_object *o, *x, *member;
    const char *s;

    o = NULL;
    ok (json_object_get_type (o) == json_type_null,
        "null pointer equates to NULL object type");
    s = json_object_to_json_string (o);
    diag ("%s", s);
    x = json_tokener_parse (s);
    ok (x == NULL,
        "null: json_tokener_parse on null returned null");
    json_object_put (o); // safe?

    o = json_object_new_object ();
    ok (o != NULL && json_object_get_type (o) == json_type_object,
        "null: json_object_new_object works");
    json_object_object_add (o, "testnull", NULL);
    ok (json_object_object_length (o) == 1,
        "null: added null field to an object");
    s = json_object_to_json_string (o);
    diag ("null: %s", s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "null: json_tokener_parse worked");
    ok (json_object_get_type (x) == json_type_object,
        "null: parsed object is still type object");
    member = o;
    ok (json_object_object_get_ex (o, "testnull", &member) && member == NULL,
        "null: json_object_object_get_ex got null field");
    json_object_put (o);
    json_object_put (x);
}

void boolean_test (bool value, const char *desc)
{
    json_object *o, *x;
    const char *s;

    o = json_object_new_boolean (value);
    ok (o != NULL && json_object_get_type (o) == json_type_boolean,
        "%s: json_object_new_boolean works", desc);
    s = json_object_to_json_string (o);
    diag ("%s: %s", desc, s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "%s: json_tokener_parse worked", desc);
    ok (json_object_get_type (x) == json_type_boolean,
        "%s: parsed object is still type boolean", desc);
    ok (json_object_get_boolean (x) == value,
        "%s: parsed object is correct value", desc);
    json_object_put (o);
    json_object_put (x);
}

void double_test (double value, const char *desc)
{
    json_object *o, *x;
    const char *s;

    o = json_object_new_double (value);
    ok (o != NULL && json_object_get_type (o) == json_type_double,
        "%s: json_object_new_double works", desc);
    s = json_object_to_json_string (o);
    diag ("%s: %s", desc, s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "%s: json_tokener_parse worked", desc);
    ok (json_object_get_type (x) == json_type_double,
        "%s: parsed object is still type double", desc);
    ok (json_object_get_double (x) == value,
        "%s: parsed object is correct value", desc);
    json_object_put (o);
    json_object_put (x);
}

void int64_test (int64_t value, const char *desc)
{
    json_object *o, *x;
    const char *s;

    o = json_object_new_int64 (value);
    ok (o != NULL && json_object_get_type (o) == json_type_int,
        "%s: json_object_new_int64 works", desc);
    s = json_object_to_json_string (o);
    diag ("%s: %s", desc, s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "%s: json_tokener_parse worked", desc);
    ok (json_object_get_type (x) == json_type_int,
        "%s: parsed object is still type int", desc);
    ok (json_object_get_int64 (x) == value,
        "%s: parsed object is correct value", desc);
    json_object_put (o);
    json_object_put (x);
}

void int_test (int32_t value, const char *desc)
{
    json_object *o, *x;
    const char *s;

    o = json_object_new_int (value);
    ok (o != NULL && json_object_get_type (o) == json_type_int,
        "%s: json_object_new_int works", desc);
    s = json_object_to_json_string (o);
    diag ("%s: %s", desc, s);
    x = json_tokener_parse (s);
    ok (x != NULL,
        "%s: json_tokener_parse worked", desc);
    ok (json_object_get_type (x) == json_type_int,
        "%s: parsed object is still type int", desc);
    ok (json_object_get_int (x) == value,
        "%s: parsed object is correct value", desc);
    json_object_put (o);
    json_object_put (x);
}

void scalar_test (void)
{
    int_test (0, "zero");
    int_test (INT32_MAX, "INT32_MAX");
    int_test (INT32_MIN, "INT32_MIN");
    int64_test (INT64_MAX, "INT64_MAX");
    int64_test (INT64_MIN, "INT64_MIN");
    //double_test (0, "zero"); // becomes an int
    double_test (DBL_MAX, "DBL_MAX");
    double_test (DBL_MIN, "DBL_MIN");
    double_test (FLT_MAX, "FLT_MAX");
    double_test (FLT_MIN, "FLT_MIN");
    boolean_test (false, "false");
    boolean_test (true, "true");
    string_test ("hello world", "hello");
    string_test ("", "emptystring");
    string_test ("\042 \134 \057 \010 \014 \012 \015 \011 \177", "escapes");
    string_test ("\\\" \\\\ \\/ \\b \\f \\n \\r \\t \\uffff", "json-escapes");
    null_test ();
}

void array_test (void)
{
    json_object *o, *member;
    int rc;

    o = json_object_new_array ();
    ok (o != NULL && json_object_get_type (o) == json_type_array,
        "json_object_new_array works");
    if (!o)
        BAIL_OUT ("cannot continue");
    member = json_object_new_string ("hello world");
    ok (member != NULL && json_object_get_type (member) == json_type_string,
        "json_object_new_string works");
    rc = json_object_array_add (o, member);
    ok (rc == 0 && member->_ref_count == 1,
        "json_object_array_add works, member refcount=1");
    ok (json_object_array_length (o) == 1,
        "json_object_array_length returns 1");

    member = json_object_new_boolean (true);
    ok (member != NULL && json_object_get_type (member) == json_type_boolean,
        "json_object_new_boolean works");
    rc = json_object_array_put_idx (o, 1, member);
    ok (rc == 0 && member->_ref_count == 1,
        "json_object_array_put_idx works, member refcount=1");
    ok (json_object_array_length (o) == 2,
        "json_object_array_length returns 2");

    diag ("%s", json_object_to_json_string_ext (o, JSON_C_TO_STRING_SPACED));

    json_object_put (o);
}

void print_test (void)
{
    json_object *o;
    const char *s;

    o = json_tokener_parse (json1);
    ok (o != NULL && json_object_get_type (o) == json_type_object,
        "parsed simple object");
    if (!o)
        BAIL_OUT ("cannot continue");

    s = json_object_to_json_string (o);
    ok (s != NULL,
        "json_object_to_json_string works");
    diag ("%s", s);

    /* same as above */
    s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_SPACED);
    ok (s != NULL,
        "json_object_to_json_string_ext SPACED works");
    diag ("%s", s);

    s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PRETTY);
    ok (s != NULL,
        "json_object_to_json_string_ext PRETTY works");
    diag ("%s", s);

    s = json_object_to_json_string_ext (o, JSON_C_TO_STRING_PLAIN);
    ok (s != NULL,
        "json_object_to_json_string_ext PLAIN works");
    diag ("%s", s);

    json_object_put (o);
}

void object_refcount_test (void)
{
    json_object *o, *member;

    o = json_object_new_object ();
    ok (o != NULL && json_object_get_type (o) == json_type_object
        && o->_ref_count == 1,
        "json_object_new_object works, refcount=1");
    if (!o)
        BAIL_OUT ("cannot continue");
    json_object_get (o);
    ok (o->_ref_count == 2,
        "json_object_get incr refcount");
    json_object_put (o);
    ok (o->_ref_count == 1,
        "json_object_put decr refcount");

    member = json_object_new_int (42);
    ok (member != NULL && json_object_get_type (member) == json_type_int
        && member->_ref_count == 1,
        "json_object_new_int works, refcount=1");
    json_object_object_add (o, "testint", member);
    ok (member->_ref_count == 1,
        "json_object_object_add and refcount remains 1 ");

    json_object_get (member);
    ok (member->_ref_count == 2,
        "json_object_get on member incr its refcount");
    json_object_put (o);
    ok (member->_ref_count == 1,
        "json_object_put on container decr member refcount");
    json_object_put (member);
}

int main(int argc, char** argv)
{
    plan (NO_PLAN);

    object_refcount_test ();
    print_test ();
    array_test ();
    scalar_test ();

    done_testing ();
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
