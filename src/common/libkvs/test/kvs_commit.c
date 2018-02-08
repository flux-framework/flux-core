#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs_commit.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_fence (NULL, 0, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_kvs_fence fails on bad input");

    errno = 0;
    ok (flux_kvs_commit (NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_kvs_commit fails on bad input");
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

