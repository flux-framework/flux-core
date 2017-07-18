#include <string.h>
#include <jansson.h>

#include "src/common/libkvs/jansson_dirent.h"
#include "src/common/libtap/tap.h"

void jdiag (json_t *o)
{
    if (o) {
        char *tmp = json_dumps (o, JSON_COMPACT);
        diag ("%s", tmp);
        free (tmp);
    } else
        diag ("nil");
}

int main (int argc, char *argv[])
{
    json_t *d1;
    json_t *d2;
    json_t *dir;
    char *s;

    plan (NO_PLAN);

    d1 = j_dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    d2 = j_dirent_create ("FILEREF", "sha1-fbedb4eb241948f6f802bf47d95ec932e9d4deaf");
    ok (d1 && d2,
        "j_dirent_create FILEREF works");
    jdiag (d1);
    jdiag (d2);
    ok (j_dirent_match (d1, d2),
        "j_dirent_match says identical dirents match");
    ok (j_dirent_validate (d1) == 0 && j_dirent_validate (d2) == 0,
        "j_dirent_validate says they are valid");
    json_decref (d1);
    json_decref (d2);

    /* ownership of new objects transferred to dirents */
    d1 = j_dirent_create ("FILEVAL", json_integer (42));
    d2 = j_dirent_create ("FILEVAL", json_string ("hello world"));
    ok (d1 && d2,
        "j_dirent_create FILEVAL works");
    jdiag (d1);
    jdiag (d2);
    ok (!j_dirent_match (d1, d2),
        "j_dirent_match says different dirents are different");
    ok (j_dirent_validate (d1) == 0 && j_dirent_validate (d2) == 0,
        "j_dirent_validate says they are valid");
    json_decref (d1);
    json_decref (d2);

    dir = json_object ();
    json_object_set_new (dir, "foo", j_dirent_create ("FILEVAL", json_integer (33)));
    json_object_set_new (dir, "bar", j_dirent_create ("FILEVAL", json_string ("Mrrrrnn?")));
    d1 = j_dirent_create ("DIRVAL", dir);
    ok (d1 != NULL,
        "j_dirent_create DIRVAL works");
    ok (j_dirent_validate (d1) == 0,
        "j_dirent_validate says it is valid");
    jdiag (d1);
    json_decref (d1);

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
