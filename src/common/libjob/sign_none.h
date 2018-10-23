#ifndef _SIGN_NONE_H
#define _SIGN_NONE_H

/* sign wrap/unwrap to be used for job submission/ingest
 * when flux-security is unavailable.  This simplified version
 * assumes mech=none and is for flux-core internal use only.
 */

#include <stdint.h>

char *sign_none_wrap (const void *payload, int payloadsz,
                      uint32_t userid);

int sign_none_unwrap (const char *input,
                      void **payload, int *payloadsz,
                      uint32_t *userid);

#endif /* !_SIGN_NONE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
