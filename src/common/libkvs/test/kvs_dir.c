#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs_dir.h"
#include "src/common/libtap/tap.h"

void test_error (void)
{
    kvsdir_t *dir;

    errno = 0;
    dir = kvsdir_create (NULL, NULL, NULL, NULL);
    ok (dir == NULL && errno == EINVAL,
        "kvsdir_create with all NULL args fails with EINVAL");
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_error ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

