#ifndef LIBUTIL_MACROS_H
#define LIBUTIL_MACROS_H 1

#define REAL_STRINGIFY(X) #X
#define STRINGIFY(X) REAL_STRINGIFY (X)
#define SIZEOF_FIELD(type, field) sizeof (((type *)0)->field)

/* Maximum size of buffer needed to decode a base64 string of length 'x',
 * where 4 characters are decoded into 3 bytes.  Add 3 bytes to ensure a
 * partial 4-byte chunk will be accounted for during integer division.
 * This size is safe for use with all (4) libsodium base64 variants.
 * N.B. unlike @dun's base64 implementation from the munge project,
 * this size does not include room for a \0 string terminator.
 */
#define BASE64_DECODE_SIZE(x) ((((x) + 3) / 4) * 3)

#endif
