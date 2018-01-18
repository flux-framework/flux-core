#ifndef _UTIL_GETIP_H
#define _UTIL_GETIP_H

/* Get ip address associated with primary hostname.
 * Return it as a string in buf (up to len bytes, always null terminated)
 * Return 0 on success, -1 on error with error message written to errstr
 * if non-NULL.
 */
int ipaddr_getprimary (char *buf, int len, char *errstr, int errstrsz);

#endif /* !_UTIL_GETIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
