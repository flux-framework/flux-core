#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libtap/tap.h"
#include "src/modules/kvs/json_util.h"
#include "src/modules/kvs/types.h"

int main (int argc, char *argv[])
{
    json_object *obj, *cpy, *o;
    href_t ref;
    const char *s;

    plan (NO_PLAN);

    ok ((obj = json_object_new_object ()) != NULL,
        "json_object_new_object works");
    json_object_object_add (obj, "A", json_object_new_string ("foo"));
    json_object_object_add (obj, "B", json_object_new_string ("bar"));
    json_object_object_add (obj, "C", json_object_new_string ("cow"));

    ok ((cpy = json_object_copydir (obj)) != NULL,
        "json_object_copydir works");

    /* first manually verify */
    ok (json_object_object_get_ex (cpy, "A", &o),
        "json_object_object_get_ex got object A");
    ok ((s = json_object_get_string (o)) != NULL,
        "json_object_get_string got string A");
    ok (strcmp (s, "foo") == 0,
        "string A is correct");

    ok (json_object_object_get_ex (cpy, "B", &o),
        "json_object_object_get_ex got object B");
    ok ((s = json_object_get_string (o)) != NULL,
        "json_object_get_string got string B");
    ok (strcmp (s, "bar") == 0,
        "string B is correct");

    ok (json_object_object_get_ex (cpy, "C", &o),
        "json_object_object_get_ex got object C");
    ok ((s = json_object_get_string (o)) != NULL,
        "json_object_get_string got string C");
    ok (strcmp (s, "cow") == 0,
        "string C is correct");

    ok (json_compare (cpy, obj) == true,
        "json_compare returns true on duplicate");

    json_object_object_add (obj, "D", json_object_new_string ("dumdum"));

    ok (json_compare (cpy, obj) == false,
        "json_compare returns false on not duplicate");

    ok (json_hash ("sha1", obj, ref) == 0,
        "json_hash works on sha1");

    ok (json_hash ("foobar", obj, ref) < 0,
        "json_hash error on bad hash name");

    json_object_put (obj);
    json_object_put (cpy);

    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
