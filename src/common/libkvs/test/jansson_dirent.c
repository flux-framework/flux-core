#include <string.h>
#include <jansson.h>

#include "src/common/libkvs/jansson_dirent.h"
#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    json_t *d1;
    json_t *d2;
    json_t *dir;
    json_t *array = NULL;
    char *s;

    plan (NO_PLAN);

    d1 = j_dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    d2 = j_dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    ok (d1 && d2,
        "j_dirent_create FILEREF works");
    ok (j_dirent_match (d1, d2),
        "j_dirent_match says identical dirents match");
    ok (j_dirent_validate (d1) == 0 && j_dirent_validate (d2) == 0,
        "j_dirent_validate says they are valid");

    j_dirent_append (&array, "foo", d1);
    j_dirent_append (&array, "bar", d2);
    ok (array != NULL && json_array_size (array) == 2,
        "j_dirent_append works"); 
    /* ownership of d1, d2 transferred to array */

    s = json_dumps (array, JSON_ENCODE_ANY);
    diag ("ops: %s", s);
    free (s);
    
    d1 = j_dirent_create ("DIRREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    d2 = j_dirent_create ("DIRREF", "sha1-aaaaa4eb241948f6f802bf47d95ec932e9d4deaf");
    ok (d1 && d2,
        "j_dirent_create DIRREF works");
    ok (!j_dirent_match (d1, d2),
        "j_dirent_match says different dirents are different");
    ok (j_dirent_validate (d1) == 0 && j_dirent_validate (d2) == 0,
        "j_dirent_validate says they are valid");

    j_dirent_append (&array, "baz", d1);
    j_dirent_append (&array, "urp", d2);
    ok (array != NULL && json_array_size (array) == 4,
        "j_dirent_append works"); 
    /* ownership of d1, d2 transferred to array */

    s = json_dumps (array, JSON_ENCODE_ANY);
    diag ("ops: %s", s);
    free (s);

    /* ownership of new objects transferred to dirents */
    d1 = j_dirent_create ("FILEVAL", json_integer (42));
    d2 = j_dirent_create ("FILEVAL", json_string ("hello world"));
    ok (d1 && d2,
        "j_dirent_create FILEVAL works");
    ok (!j_dirent_match (d1, d2),
        "j_dirent_match says different dirents are different");
    ok (j_dirent_validate (d1) == 0 && j_dirent_validate (d2) == 0,
        "j_dirent_validate says they are valid");
    j_dirent_append (&array, "baz", d1);
    j_dirent_append (&array, "urp", d2);
    ok (array != NULL && json_array_size (array) == 6,
        "j_dirent_append works"); 

    s = json_dumps (array, JSON_ENCODE_ANY);
    diag ("ops: %s", s);
    free (s);

    dir = json_object ();
    json_object_set_new (dir, "foo", j_dirent_create ("FILEVAL", json_integer (33)));
    json_object_set_new (dir, "bar", j_dirent_create ("FILEVAL", json_string ("Mrrrrnn?")));
    d1 = j_dirent_create ("DIRVAL", dir);
    ok (d1 != NULL,
        "j_dirent_create DIRVAL works");
    ok (j_dirent_validate (d1) == 0,
        "j_dirent_validate says it is valid");
    j_dirent_append (&array, "mmm", d1);
    ok (array != NULL && json_array_size (array) == 7,
        "j_dirent_append works"); 

    s = json_dumps (array, JSON_ENCODE_ANY);
    diag ("ops: %s", s);
    free (s);

    j_dirent_append (&array, "xxx", NULL);
    ok (array != NULL && json_array_size (array) == 8,
        "j_dirent_append allowed op with NULL dirent (unlink op)");

    s = json_dumps (array, JSON_ENCODE_ANY);
    diag ("ops: %s", s);
    free (s);

    json_decref (array);

    /* jansson:  How is "null" decoded?  How is NULL encoded? */
    json_t *o;
    char *str;
    o = json_loads ("null", JSON_DECODE_ANY, NULL);
    ok (o != NULL,
        "json_loads (\"null\") decodes as valid json_t");
    str = json_dumps (o, JSON_ENCODE_ANY);
    ok (str != NULL && !strcmp (str, "null"),
        "json_dumps encodes returned object as \"null\"");
    free (str);
    json_decref (o);
    s = json_dumps (NULL, JSON_ENCODE_ANY);
    ok (s == NULL,
        "json_dumps (NULL) returns NULL, which is a failure");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
