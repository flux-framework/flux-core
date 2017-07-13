#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/modules/kvs/json_util.h"
#include "src/modules/kvs/types.h"

int main (int argc, char *argv[])
{
    json_t *obj, *cpy, *o;
    href_t ref;
    const char *s;

    plan (NO_PLAN);

    obj = json_object ();
    json_object_set_new (obj, "A", json_string ("foo"));
    json_object_set_new (obj, "B", json_string ("bar"));
    json_object_set_new (obj, "C", json_string ("cow"));

    ok ((cpy = json_object_copydir (obj)) != NULL,
        "json_object_copydir works");

    /* first manually verify */
    ok ((o = json_object_get (cpy, "A")) != NULL,
        "json_object_get got object A");
    ok ((s = json_string_value (o)) != NULL,
        "json_string_value got string A");
    ok (strcmp (s, "foo") == 0,
        "string A is correct");

    ok ((o = json_object_get (cpy, "B")) != NULL,
        "json_object_get got object B");
    ok ((s = json_string_value (o)) != NULL,
        "json_string_value got string B");
    ok (strcmp (s, "bar") == 0,
        "string B is correct");

    ok ((o = json_object_get (cpy, "C")) != NULL,
        "json_object_get got object C");
    ok ((s = json_string_value (o)) != NULL,
        "json_string_value got string C");
    ok (strcmp (s, "cow") == 0,
        "string C is correct");

    ok (json_compare (cpy, obj) == true,
        "json_compare returns true on duplicate");

    json_object_set_new (obj, "D", json_string ("dumdum"));

    ok (json_compare (cpy, obj) == false,
        "json_compare returns false on not duplicate");

    ok (json_hash ("sha1", obj, ref) == 0,
        "json_hash works on sha1");

    ok (json_hash ("foobar", obj, ref) < 0,
        "json_hash error on bad hash name");

    json_decref (obj);
    json_decref (cpy);

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
