/*
 * This code was taken from git's source, as such it falls under git's license.
 * Any modifications made to it carry the same license.
 *
 * See: https://github.com/git/git/blob/master/COPYING
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#ifndef APPROXIDATE_H
#define APPROXIDATE_H

#include <sys/time.h>

/**
 * @param date The date string
 * @param tv Where the time will be placed.
 *
 * @return 0 on success
 * @return 1 on error
 */
int approxidate(const char *date, struct timeval *tv);

#endif