#ifndef _FLUX_CORE_INFO_H
#define _FLUX_CORE_INFO_H

int flux_info (flux_t h, int *rankp, int *sizep, bool *treerootp);
int flux_rank (flux_t h);
int flux_size (flux_t h);
bool flux_treeroot (flux_t h);

/* flux_getattr is used to read misc. attributes internal to the cmbd.
 * The caller must dispose of the returned string with free ().
 * The following attributes are valid:
 *    cmbd-snoop-uri   The name of the socket to be used by flux-snoop.
 *    cmbd-parent-uri  The name of parent socket.
 *    cmbd-request-uri The name of request socket.
 */
char *flux_getattr (flux_t h, int rank, const char *name);

#endif /* !_FLUX_CORE_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
