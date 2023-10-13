#include <flux/core.h>
#include "die.h"

int main (int argc, char **argv)
{
    flux_t *h;
    uint32_t rank;

    if (!(h = flux_open (NULL, 0)))
        die ("could not connect to broker");
    if (flux_get_rank (h, &rank) < 0)
        die ("could not get rank");
    printf ("My rank is %d\n", (int)rank);
    flux_close (h);
    return (0);
}
