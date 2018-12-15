#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs_classic.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_unwatch (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_unwatch fails on bad input");

    errno = 0;
    ok (flux_kvs_watch_once (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_watch_once fails on bad input");

    errno = 0;
    ok (flux_kvs_watch (NULL, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_watch fails on bad input");

    errno = 0;
    ok (flux_kvs_watch_once_dir (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_watch_once_dir fails on bad input");

    errno = 0;
    ok (flux_kvs_watch_dir (NULL, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_watch_dir fails on bad input");
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

