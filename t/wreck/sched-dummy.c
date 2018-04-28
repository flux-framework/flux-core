#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

/*
 *  Dummy sched module, do nothing but answer pings
 */
int mod_main (flux_t *h, int argc, char *argv[])
{
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        return (-1);
    return (0);
}

/* This dso extends a comms module named "parent".
 */
MOD_NAME ("sched");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
