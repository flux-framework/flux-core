#include "sha1.h"

#define SHA1_PREFIX_STRING  "sha1-"
#define SHA1_PREFIX_LENGTH  5
#define SHA1_STRING_SIZE    (SHA1_DIGEST_SIZE*2 + SHA1_PREFIX_LENGTH + 1)

int sha1_strtohash (const char *s, uint8_t hash[SHA1_DIGEST_SIZE]);
void sha1_hashtostr (const uint8_t hash[SHA1_DIGEST_SIZE],
                     char s[SHA1_STRING_SIZE]);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
