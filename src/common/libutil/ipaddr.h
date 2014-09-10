#ifndef _UTIL_GETIP_H
#define _UTIL_GETIP_H

/* Get ip address associated with primary hostname.
 * Return it as a string in buf (up to len bytes, always null terminated)
 * Calls err_exit() internally on error.
 */
void ipaddr_getprimary (char *buf, int len);

#endif /* !_UTIL_GETIP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
