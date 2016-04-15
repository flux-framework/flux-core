#ifndef _UTIL_POPEN2_H
#define _UTIL_POPEN2_H

struct popen2_child;

struct popen2_child *popen2 (const char *path, char *const argv[]);

int popen2_get_fd (struct popen2_child *p);

int pclose2 (struct popen2_child *p);

#endif /* !_UTIL_POPEN2_H */
