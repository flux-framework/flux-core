#include <math.h>
#include <flux/core.h>
#include "src/common/libutil/log.h"

int tree_height (uint32_t n, int k)
{
    return (int)floor (log (n) / log (k));
}

int main (int argc, char **argv)
{
    flux_t h;
    uint32_t n, rank;
    int k;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    if (flux_info (h, &rank, &n, &k) < 0)
        err_exit ("flux_info");
    printf ("height of %d-ary tree of size %u: %d\n",
            k, n, tree_height (n, k));
    printf ("height of %d-ary at rank %u: %d\n",
            k, rank, tree_height (rank + 1, k));
    flux_close (h);
    return (0);
}
