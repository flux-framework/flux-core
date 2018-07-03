#if HAVE_CONFIG_H
#include <config.h>
#endif

int main (int argc, char **argv)
{
#if HAVE_FLUX_SECURITY
    return 0;
#else
    return 1;
#endif
}

/* vi: ts=4 sw=4 expandtab
 */
