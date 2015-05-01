#ifndef _BROKER_SNOOP_H
#define _BROKER_SNOOP_H

typedef struct snoop_struct *snoop_t;

snoop_t snoop_create (void);
void snoop_destroy (snoop_t sn);

void snoop_set_sec (snoop_t sn, flux_sec_t sec);
void snoop_set_zctx (snoop_t sn, zctx_t *zctx);

/* Get/set snoop uri.  The set argument may be a URI wildcard.
 * The get triggers bind and resolution of URI wildcard on first use.
 */
void snoop_set_uri (snoop_t sn, const char *fmt, ...);
const char *snoop_get_uri (snoop_t sn);

/* If snoop socket has not been bound, no-op (return 0).
 * Otherwise duplicate 'zmsg' and send it.
 */
int snoop_sendmsg (snoop_t sn, zmsg_t *zmsg);

#endif /* !_BROKER_SNOOP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
