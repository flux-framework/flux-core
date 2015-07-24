#include <flux/core.h>
#include "src/common/libutil/log.h"

int main (int argc, char **argv)
{
    flux_t h;

    if (!(h = flux_open (NULL, 0)))
        err_exit ("flux_open");
    printf ("My rank is %d\n", flux_rank (h));
    flux_close (h);
    return (0);
}
