#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libjob/job.h"

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errno = 0;
    ok (flux_job_add (NULL, NULL, 0) == NULL && errno == EINVAL,
        "flux_job_add with NULL args fails with EINVAL");

    errno = 0;
    ok (flux_job_add_get_id (NULL, NULL) < 0 && errno == EINVAL,
        "flux_job_add_get_id with NULL args fails with EINVAL");

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
