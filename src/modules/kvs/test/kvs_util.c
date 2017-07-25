#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/modules/kvs/kvs_util.h"
#include "src/modules/kvs/types.h"

int main (int argc, char *argv[])
{
    json_t *obj;
    href_t ref;
    char *s1, *s2;
    size_t size;

    plan (NO_PLAN);

    obj = json_object ();
    json_object_set_new (obj, "A", json_string ("foo"));
    json_object_set_new (obj, "B", json_string ("bar"));
    json_object_set_new (obj, "C", json_string ("cow"));

    ok (kvs_util_json_hash ("sha1", obj, ref) == 0,
        "kvs_util_json_hash works on sha1");

    ok (kvs_util_json_hash ("foobar", obj, ref) < 0,
        "kvs_util_json_hash error on bad hash name");

    json_decref (obj);

    obj = json_object ();
    json_object_set_new (obj, "A", json_string ("a"));
    json_object_set_new (obj, "B", json_string ("b"));
    json_object_set_new (obj, "C", json_string ("c"));

    ok ((s1 = kvs_util_json_dumps (obj)) != NULL,
        "kvs_util_json_dumps works");

    /* json object is sorted and compacted */
    s2 = "{\"A\":\"a\",\"B\":\"b\",\"C\":\"c\"}";

    ok (!strcmp (s1, s2),
        "kvs_util_json_dumps dumps correct string");

    ok (kvs_util_json_encoded_size (obj, NULL) == 0,
        "kvs_util_json_encoded_size works w/ NULL size param");

    ok (kvs_util_json_encoded_size (obj, &size) == 0,
        "kvs_util_json_encoded_size works");

    ok (size == strlen (s2),
        "kvs_util_json_encoded_size returns correct size");

    free (s1);
    s1 = NULL;
    json_decref (obj);

    obj = json_null ();

    ok ((s1 = kvs_util_json_dumps (obj)) != NULL,
        "kvs_util_json_dumps works");

    s2 = "null";

    ok (!strcmp (s1, s2),
        "kvs_util_json_dumps works on null object");

    ok (kvs_util_json_encoded_size (obj, &size) == 0,
        "kvs_util_json_encoded_size works");

    ok (size == strlen (s2),
        "kvs_util_json_encoded_size returns correct size");

    free (s1);
    s1 = NULL;
    json_decref (obj);

    ok ((s1 = kvs_util_json_dumps (NULL)) != NULL,
        "kvs_util_json_dumps works on NULL pointer");

    s2 = "null";

    ok (!strcmp (s1, s2),
        "kvs_util_json_dumps works on NULL pointer");

    ok (kvs_util_json_encoded_size (NULL, &size) == 0,
        "kvs_util_json_encoded_size works on NULL pointer");

    ok (size == strlen (s2),
        "kvs_util_json_encoded_size returns correct size");

    free (s1);
    s1 = NULL;


    done_testing ();
    return (0);
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
