#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_namespace_create (NULL, NULL, 0, 5) == NULL && errno == EINVAL,
        "flux_kvs_namespace_create fails on bad input");

    errno = 0;
    ok (flux_kvs_namespace_remove (NULL, NULL) == NULL && errno == EINVAL,
        "flux_kvs_namespace_remove fails on bad input");

    ok (flux_kvs_set_namespace (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_set_namespace fails on bad input");

    errno = 0;
    ok (flux_kvs_get_namespace (NULL) == NULL && errno == EINVAL,
        "flux_kvs_get_namespace fails on bad input");

    errno = 0;
    ok (flux_kvs_get_version (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_get_version fails on bad input");

    errno = 0;
    ok (flux_kvs_wait_version (NULL, 0) < 0 && errno == EINVAL,
        "flux_kvs_wait_version fails on bad input");
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    errors ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

