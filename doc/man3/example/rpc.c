#include <flux/core.h>
#include "die.h"

int main (int argc, char **argv)
{
    flux_t *h;
    flux_future_t *f;
    const char *rankstr;

    if (!(h = flux_open (NULL, 0)))
        die ("could not connect to broker");

    if (!(f = flux_rpc_pack (h,
                             "attr.get",
			     FLUX_NODEID_ANY,
			     0,
		             "{s:s}",
			     "name", "rank")))
        die ("error sending attr.get request");

    if (flux_rpc_get_unpack (f,
			     "{s:s}",
			     "value", &rankstr) < 0)
        die ("error fetching rank");

    printf ("rank is %s\n", rankstr);

    flux_future_destroy (f);
    flux_close (h);
    return (0);
}
