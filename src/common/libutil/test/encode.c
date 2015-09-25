#include <string.h>
#include "src/common/libtap/tap.h"
#include "src/common/libutil/jsonutil.h"

static void *myfatal_h = NULL;

void myfatal (void *h, int exit_code, const char *fmt, ...)
{
    myfatal_h = h;
}

static int test_encode ()
{
    int rlen;
    char *r;
    char p[] = "abcdefghijklmnop";
    json_object *o = json_object_new_object ();

    util_json_object_add_data (o, "data", (uint8_t *) p, strlen (p)+1);
    diag ("%s", json_object_to_json_string (o));
    util_json_object_get_data (o, "data", (uint8_t **) &r, &rlen);

    ok (strcmp (p, r) == 0, "'%s'='%s'", p, r);
    ok (strlen(p)+1 == rlen, "lengths match");
    free (r);
    json_object_put (o);

    return (0);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);
    test_encode ();
    done_testing ();
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
