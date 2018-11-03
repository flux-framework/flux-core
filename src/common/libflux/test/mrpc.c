#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errno = 0;
    ok (flux_mrpc_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "flux_mrpc_aux_set mrpc=NULL fails with EINVAL");
    errno = 0;
    ok (flux_mrpc_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_mrpc_aux_get mrpc=NULL fails with EINVAL");

    done_testing();

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

