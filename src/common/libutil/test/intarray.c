#include <errno.h>

#include "src/common/libutil/intarray.h"
#include "src/common/libtap/tap.h"

int main (int argc, char **argv)
{
    int *ia;
    int len;

    plan (8);

    ok (intarray_create ("1,2,3", &ia, &len) == 0
        && len == 3 && ia != NULL && ia[0] == 1 && ia[1] == 2 && ia[2] == 3,
        "intarray_create 1,2,3 works");
    free (ia);

    ok (intarray_create ("1", &ia, &len) == 0
        && len == 1 && ia != NULL && ia[0] == 1,
        "intarray_create 1 works");
    free (ia);

    ok (intarray_create ("-1,1", &ia, &len) == 0
        && len == 2 && ia != NULL && ia[0] == -1 && ia[1] == 1,
        "intarray_create -1,1 works");
    free (ia);

    errno = 0;
    ok (intarray_create ("foo", &ia, &len) == -1 && errno == EINVAL,
        "intarray_create foo fails with EINVAL");

    errno = 0;
    ok (intarray_create ("", &ia, &len) == -1 && errno == EINVAL,
        "intarray_create empty string fails with EINVAL");

    errno = 0;
    ok (intarray_create ("1,,2", &ia, &len) == -1 && errno == EINVAL,
        "intarray_create 1,,2 fails with EINVAL");

    errno = 0;
    ok (intarray_create (",", &ia, &len) == -1 && errno == EINVAL,
        "intarray_create , fails with EINVAL");

    errno = 0;
    ok (intarray_create ("3.14", &ia, &len) == -1 && errno == EINVAL,
        "intarray_create 3.14 fails with EINVAL");

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
