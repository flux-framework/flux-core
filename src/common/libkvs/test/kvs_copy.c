#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    flux_t *h = (flux_t *)(uintptr_t)42;

    plan (NO_PLAN);

    errno = 0;
    ok (flux_kvs_copy (NULL, "a", "b", 0) == NULL && errno == EINVAL,
        "flux_kvs_copy h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_copy (h, NULL, "b", 0) == NULL && errno == EINVAL,
        "flux_kvs_copy srckey=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_copy (h, "a", NULL, 0) == NULL && errno == EINVAL,
        "flux_kvs_copy srckey=NULL fails with EINVAL");

    errno = 0;
    ok (flux_kvs_move (NULL, "a", "b", 0) == NULL && errno == EINVAL,
        "flux_kvs_move h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_move (h, NULL, "b", 0) == NULL && errno == EINVAL,
        "flux_kvs_move srckey=NULL fails with EINVAL");
    errno = 0;
    ok (flux_kvs_move (h, "a", NULL, 0) == NULL && errno == EINVAL,
        "flux_kvs_move srckey=NULL fails with EINVAL");

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

