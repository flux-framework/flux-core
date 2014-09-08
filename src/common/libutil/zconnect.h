#ifndef _UTIL_ZCONNECT_H
#define _UTIL_ZCONNECT_H

/* Create socket, set hwm, set identity, connect/bind all in one go.
 * All errors are fatal.
 */
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

#endif /* !_UTIL_ZCONNECT_H */
