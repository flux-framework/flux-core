#include <flux/core.h>
#include "die.h"

const double sync_min = 1.0;
const double sync_max = 60.0;

void sync_continuation (flux_future_t *f, void *arg)
{
    // do work here
    flux_future_reset (f);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;

    if (!(h = flux_open (NULL, 0)))
        die ("could not connect to broker");

    if (!(f = flux_sync_create (h, sync_max)))
        die ("error creating future");

    if (flux_future_then (f, sync_min, sync_continuation, NULL) < 0)
        die ("error registering continuation");

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        die ("reactor returned with error");

    flux_future_destroy (f);

    flux_close (h);
    return (0);
}
