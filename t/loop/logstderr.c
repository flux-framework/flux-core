#include <errno.h>
#include <syslog.h>
#include <flux/core.h>

int main (int argc, char *argv[])
{
    errno = EPERM;
    flux_log (NULL, LOG_WARNING, "hello");

    errno = ENOENT;
    flux_log_error (NULL, "world");

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

