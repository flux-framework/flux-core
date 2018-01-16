#ifndef _UTIL_READ_ALL_H
#define _UTIL_READ_ALL_H

#include <stdint.h>

int write_all (int fd, const uint8_t *buf, int len);
int read_all (int fd, uint8_t **bufp);

#endif /* !_UTIL_READ_ALL_H */
