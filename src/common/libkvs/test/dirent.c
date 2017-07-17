#include <string.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libkvs/json_dirent.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    json_object *d1;
    json_object *d2;
    json_object *dir;
    json_object *array = NULL;

    plan (NO_PLAN);

    d1 = dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    d2 = dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    ok (d1 && d2,
        "dirent_create FILEREF works");
    ok (dirent_match (d1, d2),
        "dirent_match says identical dirents match");
    ok (dirent_validate (d1) == 0 && dirent_validate (d2) == 0,
        "dirent_validate says they are valid");

    dirent_append (&array, "foo", d1);
    dirent_append (&array, "bar", d2);
    ok (array != NULL && json_object_array_length (array) == 2,
        "dirent_append works");
    /* ownership of d1, d2 transferred to array */

    diag ("ops: %s", Jtostr (array));

    d1 = dirent_create ("DIRREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    d2 = dirent_create ("DIRREF", "sha1-aaaaa4eb241948f6f802bf47d95ec932e9d4deaf");
    ok (d1 && d2,
        "dirent_create DIRREF works");
    ok (!dirent_match (d1, d2),
        "dirent_match says different dirents are different");
    ok (dirent_validate (d1) == 0 && dirent_validate (d2) == 0,
        "dirent_validate says they are valid");

    dirent_append (&array, "baz", d1);
    dirent_append (&array, "urp", d2);
    ok (array != NULL && json_object_array_length (array) == 4,
        "dirent_append works");
    /* ownership of d1, d2 transferred to array */

    diag ("ops: %s", Jtostr (array));

    /* ownership of new objects transferred to dirents */
    d1 = dirent_create ("FILEVAL", json_object_new_int (42));
    d2 = dirent_create ("FILEVAL", json_object_new_string ("hello world"));
    ok (d1 && d2,
        "dirent_create FILEVAL works");
    ok (!dirent_match (d1, d2),
        "dirent_match says different dirents are different");
    ok (dirent_validate (d1) == 0 && dirent_validate (d2) == 0,
        "dirent_validate says they are valid");
    dirent_append (&array, "baz", d1);
    dirent_append (&array, "urp", d2);
    ok (array != NULL && json_object_array_length (array) == 6,
        "dirent_append works");

    diag ("ops: %s", Jtostr (array));

    dir = Jnew ();
    json_object_object_add (dir, "foo", dirent_create ("FILEVAL", json_object_new_int(33)));
    json_object_object_add (dir, "bar", dirent_create ("FILEVAL", json_object_new_string ("Mrrrrnn?")));
    d1 = dirent_create ("DIRVAL", dir);
    ok (d1 != NULL,
        "dirent_create DIRVAL works");
    ok (dirent_validate (d1) == 0,
        "dirent_validate says it is valid");
    dirent_append (&array, "mmm", d1);
    ok (array != NULL && json_object_array_length (array) == 7,
        "dirent_append works");

    diag ("ops: %s", Jtostr (array));

    dirent_append (&array, "xxx", NULL);
    ok (array != NULL && json_object_array_length (array) == 8,
        "dirent_append allowed op with NULL dirent (unlink op)");

    diag ("ops: %s", Jtostr (array));

    Jput (array);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
