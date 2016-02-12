#include <math.h>
#include <flux/core.h>
#include <inttypes.h>
#include "src/common/libutil/log.h"

int tree_height (uint32_t n, int k)
{
    return (int)floor (log (n) / log (k));
}

int main (int argc, char **argv)
{
    flux_t h;
    uint32_t rank, n;
    int k;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (flux_get_rank (h, &rank) < 0)
        err_exit ("flux_get_rank");
    if (flux_get_size (h, &n) < 0)
        err_exit ("flux_get_size");
    if (flux_get_arity (h, &k) < 0)
        err_exit ("flux_get_arity");
    printf ("height of %d-ary tree of size %" PRIu32 ": %d\n",
            k, n, tree_height (n, k));
    printf ("height of %d-ary at rank %" PRIu32 ": %d\n",
            k, rank, tree_height (rank + 1, k));
    flux_close (h);
    return (0);
}
