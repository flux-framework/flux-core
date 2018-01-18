#ifndef _UTIL_GETIP_H
#define _UTIL_GETIP_H

#include <sys/types.h>

/* Get ip address associated with primary hostname.
 * Return it as a string in buf (up to len bytes, always null terminated)
 * Return 0 on success, -1 on error with error message written to errstr
 * if non-NULL.
 */
int ipaddr_getprimary (char *buf, int len, char *errstr, int errstrsz);


/* Get a list of all stringified ip addresses associated with interfaces on
 * the local host.  The result is appended to 'addrs', 'addrsz', an argz list
 * that must be initialized as described in argz_add(3).  Both AF_INET
 * and AF_INET6 address families are included in the list.  Return 0 on
 * success, -1 on error with error message written to errstr if non-NULL.
 */
int ipaddr_getall (char **addrs, size_t *addrssz, char *errstr, int errstrsz);

#endif /* !_UTIL_GETIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
