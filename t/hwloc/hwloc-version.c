#include <stdio.h>
#include <hwloc.h>

int main(void)
{
#if HWLOC_API_VERSION >= 0x20000
    printf("2\n");
#else
    printf("1\n");
#endif
    return 0;
}
