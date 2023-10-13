#include <flux/core.h>
#include "die.h"

void continuation (flux_future_t *f, void *arg)
{
    const char *rankstr;

    if (flux_rpc_get_unpack (f, "{s:s}", "value", &rankstr) < 0)
        die ("error getting rank");

    printf ("rank is %s\n", rankstr);
    flux_future_destroy (f);
}

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;

    if (!(h = flux_open (NULL, 0)))
        die ("could not connect to broker");

    if (!(f = flux_rpc_pack (h,
                             "attr.get",
			     FLUX_NODEID_ANY,
			     0,
		             "{s:s}",
			     "name", "rank"))
    	|| flux_future_then (f, -1., continuation, NULL) < 0)
        die ("error sending attr.get request");

    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        die ("reactor meltdown");

    flux_close (h);
    return (0);
}
